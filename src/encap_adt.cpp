/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * encap_adt.cpp: encap-based implementation of DWARF as an ADT.
 *
 * Copyright (c) 2010, Stephen Kell.
 */

#include "encap.hpp" // no more encap_adt!
#include <iostream>

namespace dwarf
{
	namespace encap
    {
		using std::shared_ptr;
		
        //factory& factory::get_factory(const dwarf::spec::abstract_def& spec)
        //{ return abstract::factory::get_factory<die>(spec); }
        class dwarf3_factory_t : public factory
        {
            std::shared_ptr<die> encapsulate_die(Dwarf_Half tag, 
	            dieset& ds, lib::die& d, Dwarf_Off parent_off) const 
            {
                switch(tag)
                {
                    case 0: assert(false); // toplevel isn't instantiated from here
#define factory_case(name, ...) \
case DW_TAG_ ## name: { auto p = my_make_shared<encap:: name ## _die>(ds, d, parent_off); attach_to_ds(p); return p; }
#include "dwarf3-factory.h" // HACK: here ^ we avoid make_shared because of its private constructor problem
#undef factory_case
					default: 
					 /* Probably a vendor extension. We create a basic_die instead. */ 
						{ auto p = my_make_shared<encap::basic_die>(ds, d, parent_off); attach_to_ds(p); return p; }
                }
	        }
			shared_ptr<basic_die>
			create_die(Dwarf_Half tag, shared_ptr<basic_die> parent,
				opt<std::string> die_name 
					/* = opt<const std::string&>()*/) const
			{
                switch(tag)
                {
                    case 0: assert(false); // toplevel isn't instantiated from here
#define factory_case(name, ...) \
case DW_TAG_ ## name: { auto p = my_make_shared<encap:: name ## _die>(parent, die_name); attach_to_ds(p); return p; }
#include "dwarf3-factory.h" // HACK: here ^ we avoid make_shared because of its private constructor problem
#undef factory_case
                    default: assert(false);
                }
			}
			
			shared_ptr<basic_die>
			clone_die(dieset& dest_ds, shared_ptr<basic_die> p_d) const
			{
                switch(p_d->get_tag())
                {
                    case 0: return my_make_shared<encap::file_toplevel_die>(dest_ds); // toplevel isn't instantiated from here
#define factory_case(name, ...) /* use the "full" constructor*/ \
case DW_TAG_ ## name: { auto p = my_make_shared<encap:: name ## _die>(dest_ds, \
    p_d->p_parent->get_offset(), \
    p_d->m_offset, \
    p_d->cu_offset, \
    p_d->m_attrs, \
    /* p_d->m_children*/std::set<Dwarf_Off>() /* children will be handled by attach_to_ds */ ); \
    attach_to_ds(p); return p; }
#include "dwarf3-factory.h" // HACK: here ^ we avoid make_shared because of its private constructor problem
#undef factory_case
                    default: assert(false);
                }
			}
						
			
		} the_dwarf3_factory;
		factory *const factory::dwarf3_factory = &the_dwarf3_factory;
		
		factory&
		factory::for_spec(spec::abstract_def const& arg)
		{
			assert(&arg == &spec::DEFAULT_DWARF_SPEC);
			return the_dwarf3_factory;
		}
		
		// compile unit
		Dwarf_Half compile_unit_die::get_address_size() const
		{ return this->dwarf::spec::compile_unit_die::get_address_size(); }
		std::string 
		compile_unit_die::source_file_name(unsigned o) const
		{
			return "FIXME";
		}
		unsigned
		compile_unit_die::source_file_count() const
		{
			return 0;
		}		
//         
//         Die_encap_all_compile_units::subprograms_iterator
//         Die_encap_all_compile_units::subprograms_begin()
//         { 
//             auto p_seq = all_cus_sequence();    
//         	return subprograms_base_iterator(
//             	p_seq->begin(p_seq),
//                 p_seq->end(p_seq));
//         }
//         Die_encap_all_compile_units::subprograms_iterator
//         Die_encap_all_compile_units::subprograms_end()
//         { 
//             auto p_seq = all_cus_sequence();    
//         	return subprograms_base_iterator(
//                 p_seq->end(p_seq),
//                 p_seq->end(p_seq));
//         }
//         Die_encap_all_compile_units::named_children_iterator 
//         Die_encap_all_compile_units::all_named_children_begin()
//         { 
//             auto p_seq = all_cus_sequence();    
//         	return named_children_iterator(
//             	p_seq->begin(p_seq),
//                 p_seq->end(p_seq));
//         }
//         Die_encap_all_compile_units::named_children_iterator 
//         Die_encap_all_compile_units::all_named_children_end()
//         { 
//             auto p_seq = all_cus_sequence();    
//         	return named_children_iterator(
//                 p_seq->end(p_seq),
//                 p_seq->end(p_seq));
//         }
//         Die_encap_all_compile_units::base_types_iterator
//         Die_encap_all_compile_units::base_types_begin()
//         { 
//             auto p_seq = all_cus_sequence();    
//         	return base_types_base_iterator(
//             	p_seq->begin(p_seq),
//                 p_seq->end(p_seq));
//         }
//         Die_encap_all_compile_units::base_types_iterator
//         Die_encap_all_compile_units::base_types_end()
//         { 
//             auto p_seq = all_cus_sequence();    
//         	return base_types_base_iterator(
//                 p_seq->end(p_seq),
//                 p_seq->end(p_seq));
//         }
//         Die_encap_all_compile_units::pointer_types_iterator
//         Die_encap_all_compile_units::pointer_types_begin()
//         { 
//             auto p_seq = all_cus_sequence();    
//         	return pointer_types_base_iterator(
//             	p_seq->begin(p_seq),
//                 p_seq->end(p_seq));
//         }
//         Die_encap_all_compile_units::pointer_types_iterator 
//         Die_encap_all_compile_units::pointer_types_end()
//         { 
//             auto p_seq = all_cus_sequence();    
//         	return pointer_types_base_iterator(
//                 p_seq->end(p_seq),
//                 p_seq->end(p_seq));        
//         }

		// we now define getters and setters in the header only
	} // end namespace encap
} // end namespace dwarf
