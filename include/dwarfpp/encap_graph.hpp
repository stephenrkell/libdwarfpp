#ifndef DWARFPP_ENCAP_GRAPH_HPP_
#define DWARFPP_ENCAP_GRAPH_HPP_

#include <algorithm>
#include <boost/iterator_adaptors.hpp>
#include <boost/graph/graph_traits.hpp>
#include "encap.hpp"

namespace dwarf { namespace encap {
    template<typename Value = dwarf::encap::attribute_value::weak_ref>
    struct die_out_edge_iterator
    	: public boost::iterator_adaptor<die_out_edge_iterator<Value>, // Derived
                    dwarf::encap::die::attribute_map::iterator,        // Base
                	Value											   // Value
                > 
    {
    	typedef dwarf::encap::die::attribute_map::iterator Base;
        die_out_edge_iterator()
          : die_out_edge_iterator::iterator_adaptor_() {}

        explicit die_out_edge_iterator(Base p)
          : die_out_edge_iterator::iterator_adaptor_(p) {}
    
        static void increment(Base& e)
        { 
        	// we want the *next* attribute, in attribute_map order,
            // which is a reference.
        	dwarf::encap::die::attribute_map::iterator search = 
            	boost::dynamic_pointer_cast<encap::die>((*e->second.get_ref().p_ds)[
                	e->second.get_ref().referencing_off
                ])->m_attrs.find(e->second.get_ref().referencing_attr);
            while (++search != boost::dynamic_pointer_cast<encap::die>((*e->second.get_ref().p_ds)[
            	e->second.get_ref().referencing_off
            ])->m_attrs.end())
            {
            	if (search->second.get_form() == dwarf::encap::attribute_value::form::REF)
                {
                	e = search; //&(search->second.get_ref());
                    return;
                }
			}
            
            // FAIL: what to do? FIXME
            e = boost::dynamic_pointer_cast<encap::die>(
            	(*e->second.get_ref().p_ds)[e->second.get_ref().referencing_off])->m_attrs.end();
        }
        void increment() { increment(this->base_reference()); }

        static void decrement(Base& e)
        { 
        	// we want the *previous* attribute, in attribute_map order,
            // which is a reference.
        	dwarf::encap::die::attribute_map::iterator begin = 
            	boost::dynamic_pointer_cast<encap::die>((*e->second.get_ref().p_ds)[
                	e->second.get_ref().referencing_off
                ])->m_attrs.begin();
        	dwarf::encap::die::attribute_map::iterator search = 
            	boost::dynamic_pointer_cast<encap::die>((*e->second.get_ref().p_ds)[
                	e->second.get_ref().referencing_off
                ])->m_attrs.find(e->second.get_ref().referencing_attr);
            while (search-- != boost::dynamic_pointer_cast<encap::die>((*e->second.get_ref().p_ds)[
            	e->second.get_ref().referencing_off
            ])->m_attrs.begin()) 
            {
            	if (search->second.get_form() == dwarf::encap::attribute_value::form::REF)
                {
                	e = search; //&(search->second.get_ref());
                    return;
                }
			}           
            
            // FAIL: what to do? FIXME
            e = begin /*- 1*/;
        }
        void decrement() { decrement(this->base_reference()); }

		Value& dereference() const { return this->base()->second.get_ref(); }
    };
} } // end namespace dwarf::encap::

namespace boost 
{
	// specialise the boost graph_traits class for encap::dieset
    template <>
    struct graph_traits<dwarf::encap::dieset> {
        typedef std::pair<dwarf::lib::Dwarf_Off, boost::shared_ptr<dwarf::encap::die> > 
        	vertex_descriptor;
        typedef dwarf::encap::attribute_value::weak_ref edge_descriptor;
          
        typedef dwarf::encap::die_out_edge_iterator<> out_edge_iterator;

		typedef dwarf::encap::dieset::map_iterator vertex_iterator;
        
        typedef directed_tag directed_category;
        typedef allow_parallel_edge_tag edge_parallel_category;
        //typedef vertex_list_graph_tag traversal_category;
        typedef incidence_graph_tag traversal_category;
        
        typedef /*dwarf::encap::dieset::size_type*/unsigned vertices_size_type;
        typedef dwarf::encap::die::attribute_map::size_type edges_size_type;
        typedef dwarf::encap::die::attribute_map::size_type degree_size_type;
    };

    graph_traits<dwarf::encap::dieset>::vertex_descriptor
    source(
        graph_traits<dwarf::encap::dieset>::edge_descriptor e,
        const dwarf::encap::dieset& g)
    {
        return *g.map_find(e.referencing_off);
    }

    graph_traits<dwarf::encap::dieset>::vertex_descriptor
    target(
        graph_traits<dwarf::encap::dieset>::edge_descriptor e,
        const dwarf::encap::dieset& g)
    {
	    return *g.map_find(e.off);
    }
    
    inline std::pair<
        graph_traits<dwarf::encap::dieset>::out_edge_iterator,
        graph_traits<dwarf::encap::dieset>::out_edge_iterator >  
    out_edges(
        graph_traits<dwarf::encap::dieset>::vertex_descriptor u, 
        const dwarf::encap::dieset& g)
    {
        typedef graph_traits<dwarf::encap::dieset>
          ::out_edge_iterator Iter;
        
        // calculate the *first* edge, i.e. the first attr that's a reference 
        dwarf::encap::die::attribute_map::iterator first = u.second->m_attrs.begin();
        while (first != u.second->m_attrs.end() && 
        	first->second.get_form() != dwarf::encap::attribute_value::form::REF) 
            first++;
        assert(first->second.get_form() == dwarf::encap::attribute_value::form::REF
         || first == u.second->m_attrs.end());

        dwarf::encap::die::attribute_map::iterator last = u.second->m_attrs.end();
		// iterate backwards from the end until we hit either the beginning
        // or a REF attr.
            
        while (last != u.second->m_attrs.begin() // empty list => exit pronto
        	&& (--last)->second.get_form() != dwarf::encap::attribute_value::form::REF);
        // if we terminated with last == begin,
        // we may or may not be on a REF
        if (last == u.second->m_attrs.begin() && 
        	last->second.get_form() != dwarf::encap::attribute_value::form::REF) 
            	last = first = u.second->m_attrs.end();
        assert(last == u.second->m_attrs.end() || last->second.get_form() == dwarf::encap::attribute_value::form::REF);
        
        return std::make_pair(Iter(first), Iter(last));
    }

	inline std::pair<
        graph_traits<dwarf::encap::dieset>::vertex_iterator,
        graph_traits<dwarf::encap::dieset>::vertex_iterator >  
	vertices(const dwarf::encap::dieset& ds)
    {
    	return std::make_pair(
        	const_cast<dwarf::encap::dieset&>(ds).map_begin(),
        	const_cast<dwarf::encap::dieset&>(ds).map_end()
        );
	}    
    inline graph_traits<dwarf::encap::dieset>::vertices_size_type 
    num_vertices(const dwarf::encap::dieset& ds)
    {
    	return ds.map_size();
    }
    
    inline graph_traits<dwarf::encap::dieset>::degree_size_type
    out_degree(
    	graph_traits<dwarf::encap::dieset>::vertex_descriptor u,
        const dwarf::encap::dieset& ds)
    {
    	// what is the out-degree of a node? it's the # attributes that are refdie
        graph_traits<dwarf::encap::dieset>::degree_size_type count = 0;
        for (dwarf::encap::die::attribute_map::iterator search = u.second->m_attrs.begin();
            search != u.second->m_attrs.end(); search++)
        { if (search->second.get_form() == dwarf::encap::attribute_value::form::REF) 
            count++;
        }
        return count;
    }
}

#endif
