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

	dwarf::spec::abstract_dieset::bfs_policy bfs_state;
	for (dwarf::spec::abstract_dieset::iterator i(ds.begin(), bfs_state);
    		i != dwarf::spec::abstract_dieset::iterator(ds.end(), bfs_state);
            i++)
    {
    	if (!*i) assert(false);
        assert(seen_offsets.find((*i)->get_offset()) == seen_offsets.end());
        seen_offsets.insert((*i)->get_offset());
    	std::cout << **i;
    }
}

