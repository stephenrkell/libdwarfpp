#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/attr.hpp>

using std::cout; 
using std::endl;
using namespace dwarf;

int main(int argc, char **argv)
{
	std::ifstream in(argv[0]);
	core::root_die root(fileno(in));
	cout << root;
	return 0;
}
