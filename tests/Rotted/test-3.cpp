#include <dwarfpp/encap.hpp>
//#include <dwarfpp/util.hpp>
#include <functional>
#include <iostream>

// test die assignment

int main(int argc, char **argv)
{
	// open the file passed in on the command-line
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	dwarf::encap::file df(fileno(f));

	// print the dieset size
    std::cout << "dieset has " << df.get_ds().size() << " entries." << std::endl;

	// add the imported functions
    df.add_imported_function_descriptions();
    
    // now check the output
    std::cout << df.get_ds();
}
