#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <srk31/algorithm.hpp>

using std::cout; 
using std::endl;
using namespace dwarf;

int main(int argc, char **argv)
{
	using namespace dwarf::core; 
	
	cout << "Opening \"test.so\"..." << endl;
	std::ifstream in("test.so");
	core::root_die root(fileno(in));

	auto cu_seq = root.begin().children();
	assert(srk31::count(cu_seq.first, cu_seq.second) == 2);

	auto cu1 = cu_seq.first;
	auto cu2 = cu1; ++cu2;

	cout << "Walking the types of each CU" << endl;
	iterator_df<> i2 = cu2;
	unsigned count = 0;
	for (iterator_df<> i1 = cu1; i1 && i1 != cu2; ++i1, ++i2)
	{
		if (i1.is_a<type_die>())
		{
			assert(i2.is_a<type_die>());
			assert(*i1.as_a<type_die>() == *i2.as_a<type_die>());
			++count;
		}
	}
	cout << "Walked " << count << " type DIEs." << endl;
	
	return 0;
}
