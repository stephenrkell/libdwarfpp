/* Test program for libdwarfpp. */

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
	
    // encapsulate the DIEs
    dwarf::encap::file def(fileno(f));
    
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
        
        boost::optional<dwarf::encap::die&> result = 
        	def.ds().resolve_die_path(
	        	0UL, split_path, split_path.begin());
        if (result) std::cout << *result; else std::cout << "not found!";
        std::cout << std::endl;
    }
}

