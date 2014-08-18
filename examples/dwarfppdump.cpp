#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/attr.hpp>

using std::cout; 
using std::endl;
using namespace dwarf;

int main(int argc, char **argv)
{
	assert(argc > 1);
	std::ifstream in(argv[1]);
	core::root_die root(fileno(in));
	cout << root;
	return 0;
}
