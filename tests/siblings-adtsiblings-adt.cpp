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
    
    std::set<dwarf::lib::Dwarf_Off> seen_offsets;
    
    std::vector<std::string> name_to_resolve;
    name_to_resolve.push_back("test-siblings-adt-input.c");
    name_to_resolve.push_back("main");
    auto found = ds.toplevel()->resolve(name_to_resolve.begin(), name_to_resolve.end());
    assert(found);

	for (auto i = found->children_begin(); 
        i != found->children_end();
        i++)
    {
    	std::cout << **i;
    }
}

