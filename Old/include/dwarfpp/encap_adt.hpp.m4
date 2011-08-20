/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * encap.hpp: transparently-allocated, mutable representations 
 *            of libdwarf-like structures.
 *
 * Copyright (c) 2008--9, Stephen Kell.
 */

#ifndef DWARFPP_ENCAP_ADT_HPP_
#define DWARFPP_ENCAP_ADT_HPP_

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>
#include "abstract.hpp"
#include "encap.hpp"

#include <iterator_with_lens.hpp>
#include <selective_iterator.hpp>
#include <downcasting_iterator.hpp>
#include <conjoining_iterator.hpp>

#include <vector>
#include <map>
#include <string>
#include <utility>

namespace dwarf {
	namespace encap {
		using namespace dwarf::lib;
        
        // forward declarations
        //class Die_encap_base;
        class die;
        class file_toplevel_die;
        class basic_die;

        typedef die Rep;

        class basic_die : public encap::die, public virtual spec::basic_die
        {
        	friend class factory;
            typedef Die_encap_base self;
        public:


        	// special constructor used by all_compile_units
            basic_die
            (encap::dieset& ds, Dwarf_Off parent, Dwarf_Half tag, 
				Dwarf_Off offset, Dwarf_Off cu_offset, 
				const encap::die::attribute_map& attrs, 
                const encap::die_off_list& children)
             :	encap::die(ds, parent, tag, offset, cu_offset, attrs, children) {}
            // "encap" constructor
            basic_die
            (encap::dieset& ds, dwarf::lib::die& d, Dwarf_Off parent_off)
             :  encap::die(ds, d, parent_off) {}
            basic_die
            (Dwarf_Half tag, encap::dieset& ds, dwarf::lib::die& d, Dwarf_Off parent_off)
             :  encap::die(ds, d, parent_off) { Dwarf_Half t; d.tag(&t); assert(t == tag); }
            // "create" constructor
            basic_die
            (Dwarf_Half tag,
            	self& parent, 
            	boost::optional<const std::string&> name)
             :	encap::die(parent.m_ds, parent.m_offset, tag, parent.m_ds.next_free_offset(), 
             	0, encap::die::attribute_map(), encap::die_off_list()) 
                // FIXME: don't pass 0 as cu_offset
            {
            	cu_offset = 0;
                m_ds.insert(std::make_pair(m_offset, boost::shared_ptr<dwarf::encap::die>(this)));
                parent.m_children.push_back(m_offset);
                if (name) m_attrs.insert(std::make_pair(DW_AT_name, dwarf::encap::attribute_value(
                	parent.m_ds, std::string(*name))));
			}
            
            const dwarf::spec::abstract_def& get_spec() { return get_ds().get_spec(); }           

            // manually defined because they don't map to DWARF attributes
            Dwarf_Off get_offset() const { return m_offset; }
            self& get_parent() const { return dynamic_cast<self&>(*m_ds[m_parent]); }
            Dwarf_Half get_tag() const { return m_tag; }

            children_iterator children_begin()
            { return children_base_iterator(m_children.begin(), encap::die_ptr_offset_lens(m_ds)); }
            children_iterator children_end()
            { return children_base_iterator(m_children.end(), encap::die_ptr_offset_lens(m_ds)); }
		};

        typedef basic_die::children_iterator
            	adt_children_iterator;
        // typedefs for ADT implementation
        template <Dwarf_Half Tag, typename Iter = adt_children_iterator>
        struct tag_equal
        {
            bool operator()(const Iter i) const 
            { return (*i)->get_tag() == Tag; }
		};

// define the typedefs in this scope, then
typedef die::children_iterator children_iterator;
typedef die::named_children_iterator named_children_iterator;
//typedef selective_iterator<die::children_iterator, has_name>  named_children_base_iterator;
//typedef downcasting_iterator<named_children_base_iterator, Die_encap_base> named_children_iterator;
#define compile_unit_EXTRA_FUNCTION_DECLS \
	Dwarf_Half get_address_size() const;
include(`encap_typedefs_gen.inc')

        class file_toplevel_die 
            : public basic_die 
		{ 
        	typedef file_toplevel_die self;
		public:
            file_toplevel_die(dieset& ds, const die_off_list& cu_off_list) : 
                basic_die(
                    ds, 0UL, 0, 0UL, 0UL, encap::die::attribute_map(), cu_off_list) {}
                       
            abstract::iters<Rep, DW_TAG_compile_unit>::iterator compile_units_begin()
            { return abstract::iters<Rep, DW_TAG_compile_unit>::base_iterator(children_begin(), children_end()); }
            abstract::iters<Rep, DW_TAG_compile_unit>::iterator compile_units_end()
            { return abstract::iters<Rep, DW_TAG_compile_unit>::base_iterator(children_end(), children_end()); }
            
       		typedef conjoining_sequence<dwarf::encap::die::children_iterator>
            conjoined_sequence;
            typedef conjoining_iterator<dwarf::encap::die::children_iterator>
            children_iterator;

            struct is_visible
            {
            	bool operator()(children_iterator i) const
                {
                	try
                    {
						Die_encap_is_program_element *p_el = 
    	                	dynamic_cast<Die_encap_is_program_element*>(&**i);
                		return !p_el->get_visibility() 
                        	|| *p_el->get_visibility() != DW_VIS_local;
                    } catch (std::bad_cast e) { return true; }
				}
            	bool operator()(Die_encap_base& d) const
                {
                	try
                    {
						Die_encap_is_program_element& el = 
    	                	dynamic_cast<Die_encap_is_program_element&>(d);
                		return !el.get_visibility() 
                        	|| *el.get_visibility() != DW_VIS_local;
                    } catch (std::bad_cast e) { return true; }
				}
            };

            boost::shared_ptr<conjoined_sequence> all_cus_sequence()
            {
   	            auto p_seq = boost::make_shared<conjoined_sequence>();
                for (compile_units_iterator i = compile_units_begin();
            		    i != compile_units_end(); i++)
                {
            	    p_seq->append((*i)->children_begin(), (*i)->children_end());        
                }
                return p_seq;
            }

			/* We have our own named_children_iterator, and others, 
             * because we're based on a conjoined sequence. */
            typedef selective_iterator<children_iterator,
        		    encap::has_name<children_iterator> >
                    named_children_iterator;

            typedef selective_iterator<children_iterator, 
            	    encap::tag_equal<DW_TAG_subprogram, children_iterator> > 
            	    subprograms_base_iterator;
            typedef downcasting_iterator<subprograms_base_iterator, 
            	    subprogram_die > subprograms_iterator;

            typedef selective_iterator<children_iterator,
        		    encap::tag_equal<DW_TAG_base_type, children_iterator> >
                    base_types_base_iterator;
            typedef downcasting_iterator<base_types_base_iterator, 
            	    Die_encap_base_type > base_types_iterator;

            typedef selective_iterator<children_iterator,
        		    encap::tag_equal<DW_TAG_pointer_type, children_iterator> >
                    pointer_types_base_iterator;
            typedef downcasting_iterator<pointer_types_base_iterator, 
            	    Die_encap_pointer_type > pointer_types_iterator;

            subprograms_iterator subprograms_begin();
            subprograms_iterator subprograms_end();
            named_children_iterator  all_named_children_begin();
            named_children_iterator all_named_children_end();
            base_types_iterator base_types_begin();
            base_types_iterator base_types_end();
            pointer_types_iterator pointer_types_begin();
            pointer_types_iterator pointer_types_end();

            // FIXME
            children_iterator all_children_begin()
            {
            	auto p_seq = all_cus_sequence();
                return p_seq->begin(p_seq);
            } 
            children_iterator all_children_end()
            { 
            	auto p_seq = all_cus_sequence();
                return p_seq->end(p_seq);
            } 
            
            template <typename Iter>
            boost::optional<abstract::Die_abstract_base<Rep>&> 
            visible_resolve(Iter path_pos, Iter path_end)
            {
            	is_visible visible;
                abstract::Die_abstract_base<Rep> *found = 0;
            	for (compile_units_iterator cu = compile_units_begin();
                	cu != compile_units_end(); cu++)
                {
                   	//std::cerr << "Looking in compile unit " << *(*cu)->get_name() << std::endl;
            	    if (path_pos == path_end) { found = this; break; }
                    auto ret = (*cu)->named_child(*path_pos);
                    boost::optional<abstract::Die_abstract_base<Rep>&> found_under_cu 
                     = ret 
                      ? boost::optional<abstract::Die_abstract_base<Rep>&>(
                          *boost::dynamic_pointer_cast< abstract::Die_abstract_base<Rep> >(ret))
                      : boost::optional<abstract::Die_abstract_base<Rep>&>();
                     
            	    Iter cur_plus_one = path_pos; cur_plus_one++;
            	    if (cur_plus_one == path_end && found_under_cu
                	    && visible(dynamic_cast<Die_encap_base&>(*found_under_cu)))
                    { found = &*found_under_cu; break; }
                    else
                    {
                        if (!found_under_cu || 
                        	!visible(dynamic_cast<Die_encap_base&>(*found_under_cu))) continue;
                        abstract::Die_abstract_has_named_children<Rep> *p_next_hop =
                    	    dynamic_cast<abstract::Die_abstract_has_named_children<Rep> *>(
                            	&(*found_under_cu));
                	    if (!p_next_hop) continue;
                        else 
                        { 
                            auto ret = p_next_hop->resolve(++path_pos, path_end);
                        	boost::optional<abstract::Die_abstract_base<Rep>&> found_recursive 
                              = ret 
                              ? boost::optional<abstract::Die_abstract_base<Rep>&>(
                                  *boost::dynamic_pointer_cast< abstract::Die_abstract_base<Rep> >(ret))
                              : boost::optional<abstract::Die_abstract_base<Rep>&>();
                            if (found_recursive) { found = &*found_recursive; break; }
                            // else continue
                        }
                    }
                }
                if (found) return *found; else return 0;
            }
            virtual //boost::optional<abstract::Die_abstract_base<Rep>&> 
			boost::shared_ptr<spec::basic_die>
            visible_named_child(const std::string& name)
            { 
            	is_visible visible;
            	for (compile_units_iterator cu = compile_units_begin();
                	cu != compile_units_end(); cu++)
                {
				    //std::cerr << "Looking for child named " << name << std::endl;
                    for (encap::named_children_iterator i = (*cu)->named_children_begin();
                	    i != (*cu)->named_children_end();
                        i++)
                    {
                	    //std::cerr << "Testing candidate die at offset " << (*i)->get_offset() << std::endl;
                	    if (dynamic_cast<Die_encap_base *>(*i)->get_name() 
                    	    && *(dynamic_cast<Die_encap_base *>(*i)->get_name()) == name
                            && visible(*(dynamic_cast<Die_encap_base *>(*i))))
                        { 
                            //return *(dynamic_cast<Die_encap_base *>(*i));
							return (*i)->get_this();
                        }
                    }
	            }
			    return boost::shared_ptr<spec::basic_die>();
          	}            
            
            typedef selective_iterator<children_iterator, is_visible> visible_children_iterator;
            visible_children_iterator visible_children_begin() 
            { return visible_children_iterator(all_children_begin(), all_children_end(), is_visible()); }
            visible_children_iterator visible_children_end() 
            { return visible_children_iterator(all_children_end(), all_children_end(), is_visible()); }
                        
            // manual because all_compile_units is special
            boost::optional<Dwarf_Unsigned> get_language() const 
            { 
            	// return the language of the first compile unit
            	return (*(const_cast<Die_encap_all_compile_units*>(this))->
            	compile_units_begin())->language(); 
            }
            
#undef stored_type_string
#undef stored_type_flag
#undef stored_type_unsigned
#undef stored_type_signed 
#undef stored_type_base 
#undef stored_type_offset
#undef stored_type_half 
#undef stored_type_ref 
#undef stored_type_tag 
#undef stored_type_refdie 
#undef stored_type_rangelist

#undef define_getters_optional
#undef define_getset_optional    
#undef define_getters_mandatory
#undef define_getset_mandatory
#undef define_iters

#undef extra_decls_subprogram

/****************************************************************/
/* begin generated ADT includes                                 */
/****************************************************************/
#define forward_decl(t) typedef Die_encap_ ## t  t ## _die;
#define declare_base(base) 
#define base_fragment(base) 
#define initialize_base(fragment)
#define constructor(fragment, ...) 
#define begin_class(fragment, base_inits, ...) 
#define base_initializations(...)
#define end_class(fragment) 
#define stored_type_string std::string
#define stored_type_flag bool
#define stored_type_unsigned Dwarf_Unsigned
#define stored_type_signed Dwarf_Signed
#define stored_type_offset Dwarf_Off
#define stored_type_half Dwarf_Half
#define stored_type_ref Dwarf_Off
#define stored_type_tag Dwarf_Half
#define stored_type_loclist dwarf::encap::loclist
#define stored_type_address Dwarf_Addr
#define stored_type_refdie boost::shared_ptr<spec::basic_die> 
#define stored_type_refdie_is_type boost::shared_ptr<spec::type_die> 
#define stored_type_rangelist dwarf::encap::rangelist

#define attr_optional(name, stored_t)
#define super_attr_optional(name, stored_t) 
#define attr_mandatory(name, stored_t) 

#define super_attr_mandatory(name, stored_t)
#define child_tag(arg) 

#include "dwarf3-adt.h"

#undef forward_decl
#undef declare_base
#undef base_fragment
#undef initialize_base
#undef constructor
#undef begin_class
#undef base_initializations
#undef end_class
#undef stored_type_string
#undef stored_type_flag
#undef stored_type_unsigned
#undef stored_type_signed
#undef stored_type_offset
#undef stored_type_half
#undef stored_type_ref
#undef stored_type_tag
#undef stored_type_loclist
#undef stored_type_address
#undef stored_type_refdie
#undef stored_type_refdie_is_type
#undef stored_type_rangelist
#undef attr_optional
#undef attr_mandatory
#undef super_attr_optional
#undef super_attr_mandatory
#undef child_tag
/* END generated ADT includes */
	}
}    

#endif
