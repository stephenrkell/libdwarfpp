/* Test program for libdwarfpp. */

#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/adt.hpp>
#include <dwarfpp/encap.hpp> /* for pathname */

int main(int argc, char **argv)
{
	// open the file passed in on the command-line
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	dwarf::lib::file df(fileno(f));
	dwarf::lib::dieset ds(df);	
	
	std::cout << "Enter DWARF pathname, followed by newline. Ctrl-D to exit." << std::endl;
	
	std::string path;
	while (std::cin.good())
	{
		std::cin >> path;
		dwarf::encap::pathname split_path;
		std::string::size_type pos = 0;
		do
		{
			std::string::size_type next_pos = path.find("::", pos);
			if (next_pos == std::string::npos) next_pos = path.length();
			split_path.push_back(std::string(path, pos, next_pos - pos));
			if (next_pos != std::string::npos) pos = next_pos + 2;
		} while (pos < path.length());
		
		auto result = 
			ds.toplevel()->resolve_visible(split_path.begin(), split_path.end());
		if (result) std::cout << *result; else std::cout << "not found!";
		std::cout << std::endl;
	}
}

