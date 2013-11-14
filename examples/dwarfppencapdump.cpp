/* Test program for libdwarfpp: dwarfdump-alike */

#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <dwarfpp/encap.hpp>

int main(int argc, char **argv)
{
	// open the file passed in on the command-line
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	dwarf::lib::file df(fileno(f));
    
    // encapsulate the DIEs
    dwarf::encap::file def(fileno(f));
    
    std::cout << def.ds();
}
