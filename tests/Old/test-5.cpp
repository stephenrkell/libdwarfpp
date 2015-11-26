#include <dwarfpp/encap.hpp>
//#include <dwarfpp/wrap.hpp>
//#include <dwarfpp/util.hpp>
#include <functional>
#include <iostream>

// test die assignment

int main(int argc, char **argv)
{
	using namespace dwarf;
	// open the file passed in on the command-line
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	encap::file df(fileno(f));

    
    //std::cout << df.get_ds();
}
