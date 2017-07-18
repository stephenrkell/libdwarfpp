#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/attr.hpp>
#include <srk31/selective_iterator.hpp>

using std::cout;
using std::cerr;
using std::endl;
using namespace dwarf;
using dwarf::core::iterator_sibs;
using dwarf::core::base_type_die;

int main(int argc, char **argv)
{
	assert(argc > 1);
	std::ifstream in(argv[1]);
	core::root_die root(fileno(in));
	
	/* Get the sequence of children of the first CU. */
	auto iter = root.begin();
	++iter;
	assert(iter.tag_here() == DW_TAG_compile_unit);
	auto first_cu_children = iter.children();
	cerr << "First child is at " << first_cu_children.first.offset_here() << std::endl;

	cerr << "Now just the odd-offset ones" << std::endl;
	auto first_cu_odd_children = first_cu_children.subseq_with(
		[](const iterator_sibs<>& it) {
			bool ret = (it.offset_here() % 2 == 1);
			//cerr << "Pred called at 0x" << std::hex << it.offset_here() << std::dec 
			//	<< "; returning " << std::boolalpha << ret << std::endl;
			return ret;
		}
	);
	
	auto i = first_cu_odd_children.first;
	cerr << "Are they null? " << std::boolalpha << 
		!first_cu_odd_children.first << ", " << !first_cu_odd_children.second
		<< std::endl;
	
	for (; i != first_cu_odd_children.second; ++i)
	{
		cout << i << std::endl;
	}
	
	cerr << "Now just the base types" << std::endl;
	auto pred = [](const decltype(first_cu_children.first)& arg) {
		return arg.is_a<base_type_die>();
	};
	
	typedef srk31::selective_iterator<
			decltype(pred),
			decltype(first_cu_children.first)
		> my_selective_iterator;
	my_selective_iterator first_sel(first_cu_children.first, first_cu_children.second, /*first_cu_children.first, */ pred),
	last_sel(first_cu_children./*first*/second, first_cu_children.second, /* first_cu_children.second, */ pred);
	
	for (auto i = first_sel; i != last_sel; ++i)
	{
		cout << i << std::endl;
	}
	
	return 0;
}
