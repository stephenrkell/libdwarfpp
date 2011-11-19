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
		using boost::shared_ptr;
		
        //factory& factory::get_factory(const dwarf::spec::abstract_def& spec)
        //{ return abstract::factory::get_factory<die>(spec); }
        class dwarf3_factory_t : public factory
        {
            boost::shared_ptr<die> encapsulate_die(Dwarf_Half tag, 
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
				opt<const std::string&> die_name 
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
