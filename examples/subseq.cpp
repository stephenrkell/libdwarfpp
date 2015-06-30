#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/attr.hpp>

using std::cout; 
using std::endl;
using namespace dwarf;
using dwarf::core::iterator_sibs;

int main(int argc, char **argv)
{
	assert(argc > 1);
	std::ifstream in(argv[1]);
	core::root_die root(fileno(in));
	
	/* Get the sequence of children of the first CU. */
	auto iter = root.begin();
	++iter;
	auto first_cu_children = iter.children();
	auto first_cu_odd_children = first_cu_children.subseq_with(
		[](const iterator_sibs<>& it) {
			return it.offset_here() % 2 == 0;
		}
	);
	
	for (auto i = first_cu_odd_children.first; i != first_cu_odd_children.second; ++i)
	{
		cout << i.base().base() << std::endl;
	}
	
	return 0;
}
