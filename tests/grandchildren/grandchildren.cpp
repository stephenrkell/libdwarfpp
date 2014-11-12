#undef NDEBUG // assert is part of our logic
#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>

#include <srk31/algorithm.hpp>

using std::cout; 
using std::endl;
using std::vector;
using namespace dwarf;

int main(int argc, char **argv)
{
	using namespace dwarf::core; 

	// using our own debug info...
	
	std::ifstream in(argv[0]);
	assert(in);
	core::root_die r(fileno(in));

	auto vg_seq = r.grandchildren();
	for (auto i_g = std::move(vg_seq.first); i_g != vg_seq.second; ++i_g)
	{
		if (i_g.base().base().name_here())
		{
			string name = *i_g.base().base().name_here();
			cout << "Saw grandchild: " << name << " from CU at 0x" 
				<< std::hex << i_g.base().base().enclosing_cu().offset_here() << std::dec 
				<< endl;
		}
	}

	return 0;
}
