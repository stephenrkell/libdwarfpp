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
	assert(vg_seq.first != vg_seq.second);
	unsigned count = 0;
	Dwarf_Off last_saw_cu = 0ul;
	bool seen_multiple_cus = false;
	for (auto i_g = std::move(vg_seq.first); i_g != vg_seq.second; ++i_g, ++count)
	{
		/* IMPORTANT: this test needs to be statically linked! */
		Dwarf_Off cu_off = i_g.enclosing_cu().offset_here();
		if (cu_off != last_saw_cu)
		{
			if (last_saw_cu) seen_multiple_cus = true;
			last_saw_cu = cu_off;
		}
		
		if (i_g.name_here())
		{
			string name = *i_g.name_here();
			cout << "Saw grandchild: " << name << " at offset 0x" << std::hex << i_g.offset_here()
				<< " from CU at 0x" << cu_off << std::dec << endl;
		}
	}
	assert(seen_multiple_cus);
	assert(count > 0);

	return 0;
}
