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
//     template<typename Value = dwarf::encap::attribute_value::weak_ref>
//     struct sibling_dep_edge_iterator
//     	: public boost::iterator_adaptor<sibling_dep_edge_iterator<Value>, // Derived
//                     dwarf::encap::die::attribute_map::iterator,        // Base
//                 	Value											   // Value
//                 > 
//     {
//     	/* This iterator ranges over all the dependencies (out-references)
//          * at or under a particular DIE (i.e. in it or its children). We call
//          * that DIE the "sibling" to distinguish it from the patriarch "parent"
//          * that defines the graph. */
//     
//     	encap::basic_die *p_parent;
//         encap::basic_die *p_sibling;
//     	typedef dwarf::encap::die::attribute_map::iterator Base;
//         
//         die::depthfirst_iterator die_pos;
//         Base attr_pos;
//         
//         sibling_dep_edge_iterator()
//           : sibling_dep_edge_iterator::iterator_adaptor_() {}
// 
//         explicit sibling_dep_edge_iterator(Base p, encap::basic_die& parent, encap::basic_die& sibling)
//           : sibling_dep_edge_iterator::iterator_adaptor_(p), p_parent(&parent), p_sibling(&sibling)
//             {
//             	assert(p == sibling.m_attrs.end() // either it's the "parked" end sentinel, or...
//                 	||
//                     // we can grab a reference out of it and find it points to something
//                     // under the common parent
//                 	&(*parent.find_sibling_ancestor_of(
//                      dynamic_cast<abstract::Die_abstract_base<encap::die>&>(
//                             *(p_parent->get_ds()[p->second.get_ref().off])
//                         )
//                     )) == &parent);
//             }
//         explicit sibling_dep_edge_iterator(encap::basic_die& parent, encap::basic_die& sibling)
//           : p_parent(&parent), p_sibling(&sibling)
//         {
//         	move_to_next(p_parent->get_ds(), p_sibling->depthfirst_begin(), false);
//         }
//     
//         void increment() 
//         {
//             // we are currently pointing at some attribute DIE.
//             // which is a REF 
//             // *under* our head "sibling"
//             // targetting some sibling or child of our originating slice.
//             // find the next one
//             // -- first try to find the next such REF in current DIE
//             // -- else find the next DIE having one, in depthfirst order
//             dieset& ds = this->base()->second.get_ref().ds;
//             die::depthfirst_iterator dfs_start(
//             	ds.find(this->base()->second.get_ref().referencing_off), 
//                 	ds.find(p_sibling->get_offset()));
//             move_to_next(ds, dfs_start, true);
//         }
//         
//         bool is_valid_attr(dieset& ds, die::attribute_map::iterator i_attr)
//         {
//         	return i_attr->second.get_form() == attribute_value::form::REF
//             	&& i_attr->first != DW_AT_sibling // and isn't a sibling "dependency" (not a real dep)
//             	&& is_under( // ...and the target offset falls under our patriarchal slice
//                 	dynamic_cast<encap::basic_die&>(*ds[i_attr->second.get_ref().off]),
//                     *p_sibling)
//                 && &*project_to_sibling_of(
//                 	dynamic_cast<encap::basic_die&>(*ds[i_attr->second.get_ref().off],
//                     *p_sibling) != p_sibling; // and isn't a reflexive edge
//         }
// // 
// //             if ()
// //             {
// //                 
// //                 encap::basic_die& deep_target = 
// //                     dynamic_cast<encap::basic_die&>(
// //                         *(ds[i_attr->second.get_ref().off]));
// //                 encap::basic_die& deep_source = 
// //                     dynamic_cast<encap::basic_die&>(
// //                         *(ds[i_attr->second.get_ref().referencing_off]));
// //                 boost::optional<abstract::Die_abstract_base<die>&> target_sibling =
// //                     (*p_parent->children_begin())->find_sibling_ancestor_of(deep_target);
// //                 boost::optional<abstract::Die_abstract_base<die>&> source_sibling =
// //                     (*p_parent->children_begin())->find_sibling_ancestor_of(deep_source);
// // //                         std::cerr << "Found an ancestor which is a child of our patriarch? " 
// // //                         	<< target_sibling << std::endl;
// //                 
// //                 
// //                 
// //                 if (target_sibling 
// //                     && &(*target_sibling) != &(*source_sibling)
// //                     && i_attr->first != DW_AT_sibling)
// //                 {
// //                     // we've found our next home -- update and return
// // //                             std::cerr << "Found next in-bounds ref attr, at offset 0x" 
// // //                             	<< std::hex << i_dfs->get_offset() << std::dec
// // //                                 << ", attr " << i_dfs->get_ds().get_spec().attr_lookup(i_attr->first)
// // //                                 << ", source sibling at 0x" << std::hex << source_sibling->get_offset() << std::dec
// // //                                 << ", target sibling at 0x" << std::hex << target_sibling->get_offset() << std::dec
// // //                                 << std::endl;
// //                     return true;
// //                 }
// //                 //std::cerr << "Found a ref attr, at offset 0x" 
// //                 //    	<< std::hex << i_dfs->get_offset() << std::dec
// //                 //        << ", attr " << i_dfs->get_ds().get_spec().attr_lookup(i_attr->first)
// //                 //        << std::endl; 
// // 	        }
// //             return false;
// //         }
// //         
//         void move_to_next(dieset& ds, die::depthfirst_iterator dfs_start, bool can_use_base)
//         {
//         	//std::cerr << "Moving to next edge of/under node at 0x" 
//             //	<< std::hex << p_sibling->get_offset() << std::dec
//             //    << ", starting from die at 0x" << std::hex << dfs_start->get_offset()
// 	        //   	<< std::dec << std::endl;
//         	for(die::depthfirst_iterator i_dfs = dfs_start;
//                 i_dfs != p_sibling->depthfirst_end();
//                 i_dfs++)
//             {
//             	die::attribute_map::iterator i_attr; 
//                 	// if we're starting out, skip straight to the current attr plus 1
//                 if (i_dfs == dfs_start && can_use_base && i_dfs->m_attrs.find(
//                 		this->base()->second.get_ref().referencing_attr) != i_dfs->m_attrs.end())
//                 {
//                 	auto found = i_dfs->m_attrs.find(
//                 		this->base()->second.get_ref().referencing_attr);
//                     assert(found != i_dfs->m_attrs.end());
//                     i_attr = ++found;
//                 }
//                 else i_attr = i_dfs->m_attrs.begin();
//                     // otherwise just start at the first attr
//             	for (; 
//                     i_attr != i_dfs->m_attrs.end();
//                     i_attr++)
//                 {
//                 	if (is_valid_attr(ds, i_attr))
//                     {
//                     	this->base_reference() = i_attr; //->second.get_ref();
//                         return;
//                     }
//                 }
//                 // if we got here, there was no such attr, so proceed in dfs order
//                 //std::cerr << "Progressing to next DIE in depth-first order..." << std::endl;
//              }
//             // if we got here, we got to the end of the dfs order, so park at the token end
//             //std::cerr << "Didn't find any in-bounds ref attrs, parking." << std::endl;
//             this->base_reference() = p_sibling->m_attrs.end();
//         }
// 
//         void decrement()
//         {
// 			dieset& ds = p_parent->get_ds();
//             // Find the DIE from which we should start our
//             // backwards-depthfirst walk. This is either
//             // the DIE of our current attr,
//             // or depthfirst_end - 1.
//             // If depthfirst_begin == depthfirst_end, there are no DIEs, so exit
//             die::depthfirst_iterator dfs_start;
//             if (this->base() == p_sibling->m_attrs.end()) // we are not currently valid
//             {
//             	if (p_sibling->depthfirst_end() == p_sibling->depthfirst_begin())
//                 {
//                 	// no DIEs; return early
//                     return;
//                 }
//                 else
//                 {
//                 	dfs_start = p_sibling->depthfirst_end(); dfs_start--;
//                 }
//             }
//             else dfs_start = die::depthfirst_iterator( // find out current position
//             	ds.find(this->base()->second.get_ref().referencing_off), 
//                 	ds.find(p_sibling->get_offset()));
//                             
//         	for(die::depthfirst_iterator i_dfs = dfs_start;
//                 ; // no termination test; we test at the end of the loop
//                 i_dfs--)
//             {
//             	die::attribute_map::iterator i_attr; 
//                 // if we have a "current attr", skip to it minus 1
//                 if (i_dfs == dfs_start)
//                 
//                 
//                  && i_dfs->m_attrs.find(
//                 		this->base()->second.get_ref().referencing_attr) != i_dfs->m_attrs.end())
//                 {
//                 	auto found = i_dfs->m_attrs.find(
//                 		this->base()->second.get_ref().referencing_attr);
//                     assert(found != i_dfs->m_attrs.end());
//                     if (found != i_dfs->m_attrs.begin()) i_attr = --found;
//                     else goto next; // next round of dfs
//                 }
//                 else i_attr = i_dfs->m_attrs.begin();
//                     // otherwise just start at the first attr
//             	for (; 
//                     i_attr != i_dfs->m_attrs.begin() ;
//                     i_attr--)
//                 {
//                 	if (is_valid_attr(ds, i_attr))
//                     {
//                     	this->base_reference() = i_attr; //->second.get_ref();
//                         return;
//                     }
//                     if (i_attr == i_dfs->m_attrs.begin()) goto next; // next in dfs order
//                 }
//                 // if we got here, there was no such attr, so proceed in dfs order
//                 //std::cerr << "Progressing to next DIE in depth-first order..." << std::endl;
//              next:
//                 this->base_reference() = p_sibling->m_attrs.end();
//                 if (i_dfs == p_sibling->depthfirst_begin()) break;
//              }
//             // if we got here, we got to the end of the dfs order, so park at the token end
//             //std::cerr << "Didn't find any in-bounds ref attrs, parking." << std::endl;
//             this->base_reference() = p_sibling->m_attrs.end();
//         }
//         
//         //bool equal(const sibling_dep_edge_iterator<Value>& arg) const
//         //{ return this->base() == arg.base(); }
//         //bool operator==(const Base& arg) const
//         //{ return this->base() == arg; }
// 
// 		Value& dereference() const { return this->base()->second.get_ref(); }
//     };
    
    /*struct Address_of_child : 
    	public std::unary_function<encap::basic_die::children_iterator, 
        		encap::basic_die *>
    {
		encap::basic_die * operator()(encap::basic_die::children_iterator& arg) const 
        { return &(**arg); }	
	};*/
	
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
    	encap::die_off_list::const_iterator> 
	{
		typedef boost::transform_iterator<getter,
	    	encap::die_off_list::const_iterator> super;
		die_base_ptr_iterator(const dieset& ds, die_off_list::const_iterator i) : super(i, getter(ds)) {}
		
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
                    boost::dynamic_pointer_cast<dwarf::encap::basic_die>((*e.p_ds)[e.referencing_off])
                )
            )
        );
    }

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
        return boost::dynamic_pointer_cast<dwarf::encap::basic_die>(
        	base_ref.find_sibling_ancestor_of(
        		boost::dynamic_pointer_cast<dwarf::encap::basic_die>((*e.p_ds)[e.off]))
                ).get();
    }
    
    inline std::pair<
        graph_traits<dwarf::encap::basic_die>::out_edge_iterator,
        graph_traits<dwarf::encap::basic_die>::out_edge_iterator >  
    out_edges(
        graph_traits<dwarf::encap::basic_die>::vertex_descriptor u, 
        const dwarf::encap::basic_die& g)
    {
//     	return std::make_pair(
//         	dwarf::encap::sibling_dep_edge_iterator<>(
//             	const_cast<dwarf::encap::Die_encap_base&>(g),
//                 const_cast<dwarf::encap::Die_encap_base&>(*u)),
//             dwarf::encap::sibling_dep_edge_iterator<>(
//                 const_cast<dwarf::encap::Die_encap_base&>(*u).m_attrs.end(), 
//                 const_cast<dwarf::encap::Die_encap_base&>(g),
//                 const_cast<dwarf::encap::Die_encap_base&>(*u))
//         );
	return std::make_pair(
	boost::make_filter_iterator(
          dwarf::encap::ref_points_under_bound_t(std::bind2nd(dwarf::encap::ref_points_under, 
          	const_cast<dwarf::encap::basic_die *>(&g))), 
          u->all_refs_dfs_begin(), 
          u->all_refs_dfs_end()
          ),
	boost::make_filter_iterator(
          dwarf::encap::ref_points_under_bound_t(std::bind2nd(dwarf::encap::ref_points_under, 
          	const_cast<dwarf::encap::basic_die *>(&g))), 
          u->all_refs_dfs_end(), 
          u->all_refs_dfs_end()
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
