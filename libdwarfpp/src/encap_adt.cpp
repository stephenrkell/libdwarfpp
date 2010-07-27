/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * encap_adt.cpp: encap-based implementation of DWARF as an ADT.
 *
 * Copyright (c) 2010, Stephen Kell.
 */

#include "encap_adt.hpp"
#include <iostream>

namespace dwarf
{
    // specialise the get_factory template in its originating namespace
	namespace abstract
    {
        // specialisation for encap
        template<> encap::die::factory_type& abstract::factory::get_factory<encap::die>(
        	const dwarf::spec::abstract_def& spec)
        {
            assert(&spec == &dwarf::spec::dwarf3); return *encap::factory::dwarf3_factory;
        }
    }
	namespace encap
    {
        factory& factory::get_factory(const dwarf::spec::abstract_def& spec)
        { return abstract::factory::get_factory<die>(spec); }
        class dwarf3_factory_t : public factory
        {
            boost::shared_ptr<die> encapsulate_die(Dwarf_Half tag, 
	            dieset& ds, lib::die& d, Dwarf_Off parent_off) const 
            {
                switch(tag)
                { 	// FIXME: don't ALLOC_SHARED here!
                    case 0: assert(false); // all_compile_units isn't instantiated from here
#include "encap_factory_gen.inc"
                    default: assert(false);
                }
	        }
		} the_dwarf3_factory;        
        factory *const factory::dwarf3_factory = &the_dwarf3_factory;
        
        Die_encap_all_compile_units::subprograms_iterator
        Die_encap_all_compile_units::subprograms_begin()
        { 
            auto p_seq = all_cus_sequence();    
        	return subprograms_base_iterator(
            	p_seq->begin(p_seq),
                p_seq->end(p_seq));
        }
        Die_encap_all_compile_units::subprograms_iterator
        Die_encap_all_compile_units::subprograms_end()
        { 
            auto p_seq = all_cus_sequence();    
        	return subprograms_base_iterator(
                p_seq->end(p_seq),
                p_seq->end(p_seq));
        }
        Die_encap_all_compile_units::named_children_iterator 
        Die_encap_all_compile_units::all_named_children_begin()
        { 
            auto p_seq = all_cus_sequence();    
        	return named_children_iterator(
            	p_seq->begin(p_seq),
                p_seq->end(p_seq));
        }
        Die_encap_all_compile_units::named_children_iterator 
        Die_encap_all_compile_units::all_named_children_end()
        { 
            auto p_seq = all_cus_sequence();    
        	return named_children_iterator(
                p_seq->end(p_seq),
                p_seq->end(p_seq));
        }
        Die_encap_all_compile_units::base_types_iterator
        Die_encap_all_compile_units::base_types_begin()
        { 
            auto p_seq = all_cus_sequence();    
        	return base_types_base_iterator(
            	p_seq->begin(p_seq),
                p_seq->end(p_seq));
        }
        Die_encap_all_compile_units::base_types_iterator
        Die_encap_all_compile_units::base_types_end()
        { 
            auto p_seq = all_cus_sequence();    
        	return base_types_base_iterator(
                p_seq->end(p_seq),
                p_seq->end(p_seq));
        }
        Die_encap_all_compile_units::pointer_types_iterator
        Die_encap_all_compile_units::pointer_types_begin()
        { 
            auto p_seq = all_cus_sequence();    
        	return pointer_types_base_iterator(
            	p_seq->begin(p_seq),
                p_seq->end(p_seq));
        }
        Die_encap_all_compile_units::pointer_types_iterator 
        Die_encap_all_compile_units::pointer_types_end()
        { 
            auto p_seq = all_cus_sequence();    
        	return pointer_types_base_iterator(
                p_seq->end(p_seq),
                p_seq->end(p_seq));        
        }

#include "encap_src_gen.inc"

	} // end namespace encap
} // end namespace dwarf
