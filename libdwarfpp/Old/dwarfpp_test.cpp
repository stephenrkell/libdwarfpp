/* Test program for libdwarfpp. */

#include "dwarfpp_simple.hpp"
#include "dwarfpp_util.hpp"
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
	// open the file passed in on the command-line
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	dwarf::file df(fileno(f));
	
// 	Dwarf_Unsigned cu_header_length;
// 	Dwarf_Half version_stamp;
// 	Dwarf_Unsigned abbrev_offset;
// 	Dwarf_Half address_size;
// 	Dwarf_Unsigned next_cu_header;

	// recurse
	print_dies_depthfirst(df, /*topdie,*/ 0);
	
	// test the name resolution code
	dwarf::abi_information info(df);
	
	const char *first_test_path[] = { "dwarfpp_test.cpp", "main" };
	std::vector<std::string> first_test_vector(&first_test_path[0], &first_test_path[sizeof first_test_path / sizeof (const char *)]);
	std::cout << "dwarfpp_test.cpp :: main lies at offset : 0x" << std::hex 
		<< dwarf::resolve_die_path(info.get_dies(), 0UL, first_test_vector, first_test_vector.begin()) 
		<< std::dec << std::endl;
		
}
