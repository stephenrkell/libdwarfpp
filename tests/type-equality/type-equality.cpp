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
	
	cout << "Opening \"test.os\"..." << endl;
	std::ifstream in("test.os");
	core::root_die root(fileno(in));

	auto cu_seq = root.begin().children();
	assert(srk31::count(cu_seq.first, cu_seq.second) == 2);

	auto cu1 = cu_seq.first;
	auto cu2 = cu1; ++cu2;

	cout << "Walking the types of each CU" << endl;
	iterator_df<> i2 = cu2;
	unsigned types_count = 0, other_count = 0;
	set<iterator_base, iterator_base::less_by_type_equality> s;

	// We have two CUs that are duplicates of each other...
	// i1 and i2 iterate over them in lock-step
	for (iterator_df<> i1 = cu1; i1 && i1 != cu2; ++i1, ++i2)
	{
		if (i1.is_a<type_die>())
		{
			assert(i2.is_a<type_die>());
			// this dispatches to type equality
			// HACK: for now, skip it for with-data-members to avoid complications
			// with cycles/incompletes... ditto subprograms
			if (i1.is_a<with_data_members_die>()
				|| i1.as_a<type_die>()->get_concrete_type().is_a<with_data_members_die>()
				|| i1.is_a<subprogram_die>()
				|| i1.as_a<type_die>()->get_concrete_type().is_a<subprogram_die>()) continue;
			assert(*i1.as_a<type_die>() == *i2.as_a<type_die>());
			s.insert(i1);
			auto retpair = s.insert(i2);
			assert(!retpair.second); // not really inserted -- duplicate by type equality
			++types_count;
		}
		else ++other_count;
	}
	cout << "Walked " << types_count << " type DIEs." << endl;
	
	return 0;
}
