#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/attr.hpp>

using std::cout; 
using std::endl;
using namespace dwarf;

int main(int argc, char **argv)
{
	core::in_memory_root_die root;
	
	auto cu = root.make_new(root.begin(), DW_TAG_compile_unit);
	
	cout << root;
	
	return 0;
}
