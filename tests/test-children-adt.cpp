#include <dwarfpp/lib.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/adt.hpp>
//#include <dwarfpp/encap_graph.hpp>
#include <cstdio>
#include <cassert>
#include <set>

int
main(int argc,char*argv[])
{
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::lib::file
	dwarf::lib::file df(fileno(f));
    // construct a dwarf::lib::dieset
    dwarf::lib::dieset ds(df);
    
    //std::set<dwarf::lib::Dwarf_Off> seen_offsets;
    
    //std::vector<std::string> name_to_resolve;
    //name_to_resolve.push_back("test-siblings-adt-input.c");
    //auto found = ds.toplevel()->resolve(name_to_resolve.begin(), name_to_resolve.end());
    //assert(found);
    //auto cu_found = boost::dynamic_pointer_cast<dwarf::spec::compile_unit_die>(found);

	//for (auto i = cu_found->subprogram_children_begin(); 
    //    i != cu_found->subprogram_children_end();
    //    i++)
    
    // first test the usual children interface
	for (auto i = ds.toplevel()->children_begin(); 
        i != ds.toplevel()->children_end();
        i++)
    {
    	std::cout << **i;
    } 	
    // now test the filtering typed interface
    for (auto i = ds.toplevel()->compile_unit_children_begin(); 
        i != ds.toplevel()->compile_unit_children_end();
        i++)
    {
    	std::cout << **i;
        for (auto j = (*i)->subprogram_children_begin(); 
            j !=(*i)->subprogram_children_end();
            j++)
        {
    	    std::cout << **j;
        }
    }
   
}

