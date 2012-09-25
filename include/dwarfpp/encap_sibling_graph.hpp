#ifndef DWARFPP_ENCAP_SIBLING_GRAPH_HPP_
#define DWARFPP_ENCAP_SIBLING_GRAPH_HPP_

#include <algorithm>
#include <functional>
#include <boost/iterator_adaptors.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/graph/graph_traits.hpp>
#include "encap.hpp"

/* This graph represents dependencies within a sibling set of DIEs. Dependencies
 * can come from attributes of a sibling, or from one of the sibling's children. */

namespace dwarf { namespace encap {

	struct is_under_t : public std::binary_function<encap::basic_die, encap::basic_die, bool>
    {
    	bool operator()(encap::basic_die& deep, encap::basic_die& head) const
        {
            // return true if arg1 is under arg2
            return head.children_begin() != head.children_end()
            	&& (*head.children_begin())->find_sibling_ancestor_of(deep.get_ds()[deep.get_offset()]);
        }
    };
	const is_under_t is_under = {};
	struct project_to_sibling_of_t : public std::binary_function<encap::basic_die&, encap::basic_die&, 
    	boost::optional<encap::basic_die&> >
    {
    	boost::optional<encap::basic_die&> operator()(encap::basic_die& deep, encap::basic_die& head) const
        {
            if (is_under(deep, head)) return 
            	dynamic_cast<encap::basic_die&>(
                	*(
                    	(*head.children_begin())
                    		->find_sibling_ancestor_of(deep.get_ds()[deep.get_offset()])
                     )
                 );
            else return 0;
        }
    };
	const project_to_sibling_of_t project_to_sibling_of = {};
	
	struct ref_points_under_t : public std::binary_function<die::attribute_map::value_type, encap::basic_die *, bool>
    {
    	bool operator()(const die::attribute_map::value_type& ref, encap::basic_die * p_head) const
        {
        	assert(p_head);
            // return true if arg1 is under arg2
            dieset& ds = p_head->get_ds();
            auto target_die_iter = ds.map_find(ref.second.get_ref().off);
            return target_die_iter != ds.map_end()
            	&& is_under(dynamic_cast<encap::basic_die&>(*(target_die_iter->second)), *p_head);
        }
    };
	const ref_points_under_t ref_points_under = {};
     
    //typedef dwarf::encap::sibling_dep_edge_iterator<> out_edge_iterator;
    // We need this one because std::binder2nd is not defaul-constructible, but
    // our iterators get default-constructed during DFS.
    struct ref_points_under_bound_t : public std::binder2nd<dwarf::encap::ref_points_under_t>
    {
        ref_points_under_bound_t() 
        : std::binder2nd<dwarf::encap::ref_points_under_t>(
            ref_points_under, 0) {}
        ref_points_under_bound_t(std::binder2nd<dwarf::encap::ref_points_under_t> binder)
         : std::binder2nd<dwarf::encap::ref_points_under_t>(binder) {}

    };
	const ref_points_under_bound_t ref_points_under_bound = {};
	
	struct getter : public std::unary_function<Dwarf_Off, 
		encap::basic_die * >
	{
		const encap::dieset *p_ds;
		getter(const dieset& ds) : p_ds(&ds) {}
		getter() {}
		encap::basic_die * operator()(Dwarf_Off arg) const
		{ assert(p_ds);
		  auto found = p_ds->map_find(arg);
		  assert(found != p_ds->map_end());
		  return const_cast<encap::basic_die *>(
		  	dynamic_cast<const encap::basic_die*>(found->second.get())); 
		}
	};
    struct die_base_ptr_iterator
	 : public boost::transform_iterator<getter,
    	std::set<Dwarf_Off>::const_iterator> 
	{
		typedef boost::transform_iterator<getter,
	    	std::set<Dwarf_Off>::const_iterator> super;
		die_base_ptr_iterator(const dieset& ds, std::set<Dwarf_Off>::const_iterator i) : super(i, getter(ds)) {}
		
		die_base_ptr_iterator() : super() {}
	};

    
} } // end namespace dwarf::encap::

namespace boost 
{
	// specialise the boost graph_traits class for encap::dieset
    template <>
    struct graph_traits<dwarf::encap::basic_die> {
        typedef dwarf::encap::basic_die *vertex_descriptor;
        typedef dwarf::encap::attribute_value::weak_ref edge_descriptor;
          
        typedef boost::filter_iterator<        	
            //std::binder2nd<dwarf::encap::ref_points_under_t>,
            dwarf::encap::ref_points_under_bound_t,
            dwarf::encap::die::all_refs_dfs_iterator> relevant_ref_attrs_iterator;
        
        struct get_ref_t 
        : public std::unary_function<dwarf::encap::die::attribute_map::value_type,
        	edge_descriptor>
        {
        	edge_descriptor operator()(dwarf::encap::die::attribute_map::value_type attr_val) const
            { assert(attr_val.second.get_form() == dwarf::encap::attribute_value::REF);
              return attr_val.second.get_ref(); }
        };    
        
        typedef boost::transform_iterator<get_ref_t, relevant_ref_attrs_iterator> 
        	out_edge_iterator;

		typedef dwarf::encap::die_base_ptr_iterator vertex_iterator;
        
        typedef directed_tag directed_category;
        typedef allow_parallel_edge_tag edge_parallel_category;
        //typedef vertex_list_graph_tag traversal_category;
        //typedef incidence_graph_tag traversal_category;
        struct traversal_tag :
          public virtual vertex_list_graph_tag,
          public virtual incidence_graph_tag { };
        typedef traversal_tag traversal_category;
        
        typedef /*dwarf::encap::dieset::size_type*/unsigned vertices_size_type;
        typedef dwarf::encap::die::attribute_map::size_type edges_size_type;
        typedef dwarf::encap::die::attribute_map::size_type degree_size_type;
    };

	/* FIXME: get rid of the casts in here by overloading the children_begin() and sim.
     * iterator functions with const and non-const versions. */

	inline
    graph_traits<dwarf::encap::basic_die>::vertex_descriptor
    source(
        graph_traits<dwarf::encap::basic_die>::edge_descriptor e,
        const dwarf::encap::basic_die& g)
    {
    	// project the edge's source up to a child of the patriarch
        return &dynamic_cast<dwarf::encap::basic_die&>(
            *( // deref boost::optional
                dynamic_cast<dwarf::encap::basic_die&>(
                	*(
                    	*const_cast<dwarf::encap::basic_die&>(g).children_begin()
                      )
	        	).find_sibling_ancestor_of(
                    std::dynamic_pointer_cast<dwarf::encap::basic_die>((*e.p_ds)[e.referencing_off])
                )
            )
        );
    }

	inline
    graph_traits<dwarf::encap::basic_die>::vertex_descriptor
    target(
        graph_traits<dwarf::encap::basic_die>::edge_descriptor e,
        const dwarf::encap::basic_die& g)
    {
    	// project the edge's target up to a child of the patriarch
        dwarf::encap::basic_die& g_nonconst = const_cast<dwarf::encap::basic_die&>(g);
        auto begin_iter = g_nonconst.children_begin();
        dwarf::encap::basic_die *begin_ptr = dynamic_cast<dwarf::encap::basic_die *>(begin_iter->get());
        dwarf::encap::basic_die& base_ref = *begin_ptr;
        return std::dynamic_pointer_cast<dwarf::encap::basic_die>(
        	base_ref.find_sibling_ancestor_of(
        		std::dynamic_pointer_cast<dwarf::encap::basic_die>((*e.p_ds)[e.off]))
                ).get();
    }
    
    inline std::pair<
        graph_traits<dwarf::encap::basic_die>::out_edge_iterator,
        graph_traits<dwarf::encap::basic_die>::out_edge_iterator >  
    out_edges(
        graph_traits<dwarf::encap::basic_die>::vertex_descriptor u, 
        const dwarf::encap::basic_die& g)
    {
		auto u_all_refs = u->all_refs_dfs_seq();
		return std::make_pair(
			boost::make_transform_iterator(
				boost::make_filter_iterator(
					dwarf::encap::ref_points_under_bound_t(std::bind2nd(dwarf::encap::ref_points_under, 
						const_cast<dwarf::encap::basic_die *>(&g))), 
					u_all_refs->begin(), 
					u_all_refs->end()
				),
				graph_traits<dwarf::encap::basic_die>::get_ref_t()
			),
			boost::make_transform_iterator(
				boost::make_filter_iterator(
					dwarf::encap::ref_points_under_bound_t(std::bind2nd(dwarf::encap::ref_points_under, 
						const_cast<dwarf::encap::basic_die *>(&g))), 
					u_all_refs->end(), 
					u_all_refs->end()
				),
				graph_traits<dwarf::encap::basic_die>::get_ref_t()
			)
		);
    }
    inline graph_traits<dwarf::encap::basic_die>::degree_size_type
    out_degree(
    	graph_traits<dwarf::encap::basic_die>::vertex_descriptor u,
        const dwarf::encap::basic_die& g)
    {
    	// HACK: we shouldn't really do this
        unsigned count = 0;
        dwarf::encap::basic_die& vg = const_cast<dwarf::encap::basic_die&>(g);
        dwarf::encap::die::attribute_map::iterator attrs_end = vg.m_attrs.end();
        for (graph_traits<dwarf::encap::basic_die>::out_edge_iterator i = 
        		out_edges(u, g).first;
        			///*i != attrs_end*/true;
                    i != out_edges(u, g).second;
                    i++) count++;
        return count;
    }

	inline std::pair<
        graph_traits<dwarf::encap::basic_die>::vertex_iterator,
        graph_traits<dwarf::encap::basic_die>::vertex_iterator >  
	vertices(const dwarf::encap::basic_die& parent)
    {
    	return std::make_pair(
        	dwarf::encap::die_base_ptr_iterator(parent.get_ds(),
				const_cast<dwarf::encap::basic_die&>(parent).children().begin()),
			dwarf::encap::die_base_ptr_iterator(parent.get_ds(),
        	const_cast<dwarf::encap::basic_die&>(parent).children().end())
        );
	}    
    inline graph_traits<dwarf::encap::basic_die>::vertices_size_type 
    num_vertices(const dwarf::encap::basic_die& parent)
    {
    	return const_cast<dwarf::encap::die&>(
        	dynamic_cast<const dwarf::encap::die&>(
            	parent
                )
            ).children().size();
    }
}

#endif
