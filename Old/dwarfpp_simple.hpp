#ifndef __DWARFPP_SIMPLE_H
#define __DWARFPP_SIMPLE_H

#include "dwarfpp.h"
#include "dwarfpp_util.hpp"
#include <map>
#include <string>
#include <iostream>

namespace dwarf {
	void print_dies_depthfirst(dwarf::file& f,/* dwarf::die& d,*/ int indent);
	void print_globals(dwarf::file &f);
	/*void print_attributes(dwarf::die &d, int indent);*/
	
	/* We can represent a set of dies as a map of offsets to dies. In this way
	 * we can compactly store a mutually-referencing set of dies (i.e. a subtree).
	 * What about dwarf back-references? Are they also made by offset, or by some
	 * more abstract identifier? Offset, I think. */
		
	class abi_information {
		typedef std::pair<std::vector<Dwarf_Off>, dieset&> info_pair;
/*		template <typename K, V> class ref_map<K&, V> : private std::map<K, V>
		{
			public:
			V& operator [](const K& k)
			{
				return *(find(k));
			}
		};*/

		dieset m_dies; // *all* dies of any kind
		info_pair namespaces;
		info_pair imported_declarations;
		info_pair imported_modules;
		info_pair compilation_units;
		info_pair funcs;
		info_pair toplevel_vars;
		info_pair types;
		
		// list of CU offsets -- is the list of children for the fake toplevel node
		std::vector<Dwarf_Off> cu_off_list;
		
		//void add_dies_depthfirst(file &f, die& current, dieset& m, Dwarf_Off parent_off);
		void process_die(file &f, die& d/*, die_processor p*/, Dwarf_Off parent_off);
				
	public:
		abi_information(file& f);
		void print(std::ostream& o);
		
		// experiment to see whether map's copy constructor deep-copies entries
		static void test_map_copy_constructor(die& d);
		
		encap::die& operator[](Dwarf_Off o) 
		{
			if (m_dies.find(o) == m_dies.end()) assert(false);
			return m_dies.find(o)->second; 
		}
		
		dieset& get_dies() { return m_dies; }
		//encap::die& operator[](Dwarf_Off off) { return m_dies[off]; }
		
		//info_pair& get_funcs() { return funcs; }
		//info_pair *func_ptr() { return &funcs; }
		die_off_list& func_offsets() { return funcs.first; }
		//dieset& func_dies() { return funcs.second; }
		
		//info_pair& get_toplevel_vars() { return toplevel_vars; }
		//info_pair *toplevel_var_ptr() { return &toplevel_vars; }
		die_off_list& toplevel_var_offsets() { return toplevel_vars.first; }
		//dieset& toplevel_var_dies() { return toplevel_vars.second; }
		
		//info_pair& get_types() { return types; }
		//info_pair *type_ptr() { return &types; }
		die_off_list& type_offsets() { return types.first; }
		//dieset& type_dies() { return types.second; }
		
// 		info_pair& get_compilation_units() { return compilation_units; }		
// 		info_pair& get_namespaces() { return namespaces; }
// 		info_pair& get_imported_declarations() { return imported_declarations; }
// 		info_pair& get_imported_modules() { return imported_modules; }				
 		die_off_list& compilation_unit_offsets() { return compilation_units.first; }		
 		die_off_list& namespace_offsets() { return namespaces.first; }
 		die_off_list& imported_declaration_offsets() { return imported_declarations.first; }
 		die_off_list& get_imported_modules() { return imported_modules.first; }				
	};
}

#endif
