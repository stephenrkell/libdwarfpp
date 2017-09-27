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
	struct my_root_die : public core::root_die
	{
		using root_die::root_die;
		unordered_map<Dwarf_Off, basic_die* >& get_live_dies() { return this->live_dies; }
	} r(fileno(in));
	std::ofstream null_out;
	for (auto i = r.begin(); i != r.end(); ++i);
	/* Here comes the point: assert that no DIEs are live, or just the CU. */
	unsigned sz = r.get_live_dies().size();
	cout << "Live DIEs: " << sz << std::endl;
	assert(sz < 2);

	return 0;
}
