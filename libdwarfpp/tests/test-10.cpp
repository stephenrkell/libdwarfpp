#include <dwarfpp/adt.hpp>
#include <cstdio>
#include <iostream>

int
main(int argc,char*argv[])
{
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::lib::file
	dwarf::lib::file df(fileno(f));
    
    // construct a lib dieset
    dwarf::lib::dieset ds(df);
    
    // print compile units
    for (auto i = ds.begin(); i != ds.end(); i++)
    {
    	std::cout << "Found a DIE at offset " << i.pos().off << std::endl;
    }
    
    // print visible subprograms
    
}

