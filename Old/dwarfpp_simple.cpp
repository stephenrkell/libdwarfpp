#include "dwarfpp_simple.hpp"
#include "dwarfpp_util.hpp"
#include <cstdio>
#include <cassert>
#include <vector>
#include <iostream>

namespace dwarf {
	void print_dies_depthfirst(dwarf::file& f, /*dwarf::die& current,*/ int indent)
	{
		//static int indent = 0;
		// print this die first of all
		
		abi_information info(f);
		Dwarf_Off start_offset; /*current.offset(&start_offset);*/ start_offset = info.get_dies().begin()->first;
		assert(info.get_dies().find(start_offset) != info.get_dies().end());
		//func2_true<encap::die&, depthfirst_walker<T, A, S> > true_f2;
		print_action action(info);
		walker::depthfirst_walker<print_action> walk(action);
		walk(info.get_dies(), start_offset);
		
		
// 		Dwarf_Half tag; current.tag(&tag);
// 		Dwarf_Off offset; current.offset(&offset);
// 		int name_retval;
// 		char *name; name_retval = current.name(&name);
// 		//const char *name_if_present = name_retval == DW_DLV_OK ? name : "(unnamed node)";
// 		for (int i = 0; i < indent; i++) printf("\t");
// 		printf("Read a DIE: tag %s, offset %llx, name %s\n", 
// 			tag <= 64 ? dwarf::tag_lookup(tag) : "(none)", 
// 			offset, 
// 			name_retval >= 0 ? name : "(not present)");
// 		print_attributes(current, indent + 1);		
// 
// 		try
// 		{
// 			// now try exploring children
// 			//fprintf(stderr, "Trying to construct a child of %s\n", name_if_present);
// 			dwarf::die next(current); // calls dwarf_child
// 			print_dies_depthfirst(f, next, indent + 1);
// 
// 			//fprintf(stderr, "Completed traversal of subtree rooted at %s\n", name_if_present);
// 		}	
// 		catch (dwarf::No_entry e)
// 		{
// 			// current has no child, so try siblings
// 			//fprintf(stderr, "Exhausted children, trying siblings of %s\n", name_if_present);
// 		}
// 
// 		// now try exploring siblings
// 		try
// 		{
// 			// then try exploring siblings
// 			//fprintf(stderr, "Trying to construct a sibling of %s\n", name_if_present);
// 			dwarf::die next(f, current); // calls dwarf_sibling
// 			print_dies_depthfirst(f, next, indent);
// 		}
// 		catch (dwarf::No_entry e)
// 		{
// 			// then return
// 			//fprintf(stderr, "Exhausted siblings of %s, backtracking\n", name_if_present);
// 			return;
// 		}	
// 		if (name_retval >= 0) dwarf_dealloc(f.get_dbg(), name, DW_DLA_STRING);
	}

	void print_globals(dwarf::file& f)
	{
		dwarf::global_array arr(f);

		for (int i = 0; i < arr.count(); i++)
		{
			char *name;
			arr.get(i).get_name(&name);		
			printf("Found a global, name %s\n", name);
		}
	}
		
	abi_information::abi_information(file &f) :
		m_dies(),
		namespaces(std::vector<Dwarf_Off>(), m_dies),
		imported_declarations(std::vector<Dwarf_Off>(), m_dies),
		imported_modules(std::vector<Dwarf_Off>(), m_dies),
		compilation_units(std::vector<Dwarf_Off>(), m_dies),
		funcs(std::vector<Dwarf_Off>(), m_dies),
		toplevel_vars(std::vector<Dwarf_Off>(), m_dies),
		types(std::vector<Dwarf_Off>(), m_dies)
	{
		/* Here we simply traverse all DIEs and encapsulate them, selecting which map
		 * to put them in based on their tag.  Once we've decided which map is appropriate,
		 * put all children in the same map also (recursively).*/
		
		// TODO: we have to explicitly loop through CU headers, to set the CU context when getting dies
		Dwarf_Unsigned cu_header_length;
		Dwarf_Half version_stamp;
		Dwarf_Unsigned abbrev_offset;
		Dwarf_Half address_size;
		Dwarf_Unsigned next_cu_header;
		
		assert(funcs.first.size() == 0 && funcs.second.size() == 0);

		// create a fake toplevel parent die
		encap::die::attribute_map no_attrs;
		m_dies.insert(std::make_pair(0UL, encap::die(
			f, m_dies, 0UL, 0, 0UL, 0UL, no_attrs, cu_off_list))
		);
		
		// recursively process this die	-- explores siblings too!
		// first we reset the libdwarf CU context state to the initial value,
		// by looping through CU headers until we get DW_DLV_NO_ENTRY. Stupid stateful API!
		int retval;
		while(DW_DLV_OK == f.next_cu_header(&cu_header_length, &version_stamp,
			&abbrev_offset, &address_size, &next_cu_header));
			
			
		// we terminated the above loop with either DW_DLV_NO_ENTRY or DW_DLV_ERROR.
		// if the latter, the next call should return an error as well.
		// if the former, that's good; the next call should be okay
		retval = f.next_cu_header(&cu_header_length, &version_stamp,
			&abbrev_offset, &address_size, &next_cu_header);
		if (retval != DW_DLV_OK)
		{
			std::cerr << "Warning: couldn't get first CU header! no debug information imported" << std::endl;
			//throw No_entry();
		}
		else
		{
//			die first(f);
//			process_die(f, first, 0UL);
		
			for (/*int retval = f.next_cu_header(&cu_header_length, &version_stamp,
					&abbrev_offset, &address_size, &next_cu_header)*/
					// already loaded the first CU header!
					;
					retval != DW_DLV_NO_ENTRY; // termination condition (negated)
					retval = f.next_cu_header(&cu_header_length, &version_stamp,
						&abbrev_offset, &address_size, &next_cu_header))
			{
 				die first(f); // this is the CU header (really! "first" is relative to CU context)
                
                /* We should process the version stamp and address_size here.
                 * (libdwarf may abstract the address_size for us?)
                 *
                 * For the version stamp, we simply
                 */

				process_die(f, first, 0UL); // this *doesn't* explore siblings of CU header DIEs!

				// add this die to the list of toplevel children
				Dwarf_Off off; first.offset(&off);
				//m_dies[0].children().push_back(off); // don't do this -- done in process_die
// 				std::cerr << "Toplevel children offsets are now:";
// 				for (std::vector<Dwarf_Off>::iterator i = m_dies[0].children().begin();
// 					i != m_dies[0].children().end(); i++)
// 				{
// 					std::cerr << std::hex << " 0x" << *i;
// 				}
// 				std::cerr << std::dec;
			}
		}
	}
	
	void abi_information::process_die(file &f, die& d/*, die_processor p*/, Dwarf_Off parent_off)
	{
		Dwarf_Half tag; d.tag(&tag);
		Dwarf_Off offset; d.offset(&offset);
		
		// Record the relationship with our parent.
		// NOTE: this will create m_dies[parent_off] using the default constructor,
		// generating a warning, if it hasn't been created already. Typically this is
		// when passing 0UL as the toplevel parent, without creating it first.
		m_dies[parent_off].children().push_back(offset);
		if (parent_off == 0UL) std::cerr << "children of toplevel are now " << m_dies[parent_off].children().size() << std::endl;

		// encapsulate and store the die
		m_dies.insert(std::make_pair(offset, dwarf::encap::die(m_dies, d, parent_off)));
		
		switch(tag)
		{
			case DW_TAG_imported_module:
				imported_modules.first.push_back(offset);
				goto explore_children;
			case DW_TAG_imported_declaration:
				imported_declarations.first.push_back(offset);
				goto explore_children;
			case DW_TAG_namespace: 
				namespaces.first.push_back(offset);
				// depth-first
				goto explore_children;
			case DW_TAG_compile_unit: 
				compilation_units.first.push_back(offset);
				goto explore_children;
			case DW_TAG_subprogram:
				funcs.first.push_back(offset);
				//add_dies_depthfirst(f, d, funcs.second, parent_off);
				//goto explore_siblings;
				goto explore_children;			
			case DW_TAG_variable:
				toplevel_vars.first.push_back(offset);
				//add_dies_depthfirst(f, d, toplevel_vars.second, parent_off);
				//goto explore_siblings;
				goto explore_children;
			case DW_TAG_array_type:
			case DW_TAG_class_type:
			case DW_TAG_enumeration_type:
			case DW_TAG_pointer_type:
			case DW_TAG_reference_type:
			case DW_TAG_string_type:
			case DW_TAG_structure_type:
			case DW_TAG_subroutine_type:
			case DW_TAG_typedef:
			case DW_TAG_union_type:
			case DW_TAG_ptr_to_member_type:
			case DW_TAG_set_type:
			case DW_TAG_subrange_type:
			case DW_TAG_base_type:
			case DW_TAG_const_type:
			case DW_TAG_file_type:
			case DW_TAG_packed_type:
			case DW_TAG_template_type_parameter:
			case DW_TAG_thrown_type:
			case DW_TAG_volatile_type:
			case DW_TAG_restrict_type:
			case DW_TAG_interface_type:
			case DW_TAG_unspecified_type:
			case DW_TAG_mutable_type:
			case DW_TAG_shared_type:
				types.first.push_back(offset);
				//add_dies_depthfirst(f, d, types.second, parent_off);
				//goto explore_siblings;			
				goto explore_children;
			case DW_TAG_formal_parameter:
			case DW_TAG_unspecified_parameters:
			case DW_TAG_member:
			case DW_TAG_enumerator:
			case DW_TAG_lexical_block:
			case DW_TAG_label:
			case DW_TAG_inlined_subroutine:
				goto explore_children;
			default:
				std::cerr << "process_die hit unknown tag " << tag_lookup(tag) << std::endl;
				goto explore_children;
			explore_children:
				// depth-first
				try
				{
					die next(d); // get the *first* child only
					process_die(f, next, offset); // recursively process the child and its siblings
					//Dwarf_Off child_offset; next.offset(&child_offset); // remember this...
					//dies[offset].children().push_back(child_offset);	// parent-child relationship
				} catch (No_entry) {}
				goto explore_siblings;
// 				try
// 				{
// 					die next(f, d); // get sibling
// 					process_die(f, next);
// 				}
// 				catch (No_entry) {}
// 				
// 			break;
			explore_siblings:
				// now try exploring siblings
				try
				{
					dwarf::die next(f, d); // calls dwarf_sibling
					//add_dies_depthfirst(f, next, m);
					process_die(f, next, parent_off);
				}
				catch (dwarf::No_entry e) {}				
			break;
		}
	}
	
// 	void abi_information::add_dies_depthfirst(file &f, die& current, 
// 		std::map<Dwarf_Off, dwarf::encap::die>& m, Dwarf_Off parent_off)
// 	{
// 		Dwarf_Half tag; current.tag(&tag);
// 		Dwarf_Off offset; current.offset(&offset);
// 		
// 		// add this die
// 		m.insert(std::make_pair(offset, dwarf::encap::die(current, parent_off)));
// 			
// 		// this encap::die is created on the stack
// 		// so will be destructed when this function returns;
// 		// we rely on its contents being deep-copied either by make_pair or by std::map (or both)
// 		// -- does make_pair create a copy? if so, it should be a deep one
// 		// which means we need a copy constructor for dwarf::encap::die
// 		// or just for all of its members that should not be shallow-copied?
// 
// 		try
// 		{
// 			// now try adding children
// 			dwarf::die next(current); // calls dwarf_child
// 
// 			// record the parent-child relationship
// 			Dwarf_Off next_offset; next.offset(&next_offset);
// 			dies[offset].children().push_back(next_offset);
// 
// 			add_dies_depthfirst(f, next, m, offset);	// recursively add the children to this map
// 			//Dwarf_Off child_offset; next.offset(&child_offset);	// remember this...
// 			//dies[offset].children().push_back(child_offset);	/// parent-child relationship
// 		}	
// 		catch (dwarf::No_entry e) {}
// 	}
	
// 	void abi_information::print(std::ostream& o)
// 	{
// 		std::map<Dwarf_Off,dwarf::encap::die> *maps[] = { 
// 			&funcs.second, &toplevel_vars.second, &types.second };
// 		const char *map_names[] = { "functions", "toplevel variables", "types" };
// 		for (size_t i = 0; i < sizeof maps; i++)
// 		{
// 			// print name
// 			o << map_names[i] << ": " << std::endl;
// 			// print content
// 			for (std::map<Dwarf_Off, dwarf::encap::die>::iterator p = maps[i]->begin(); 
// 				p != maps[i]->end(); p++)
// 			{
// 				o << "DIE at offset 0x" << std::hex << p->first << std::dec << ": " << std::endl;
// 				p->second.print(o);
// 			}
// 		}
// 	}	
}
