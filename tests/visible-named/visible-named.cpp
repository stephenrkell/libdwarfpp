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

	std::vector<iterator_base> results
	 = r.find_all_visible_grandchildren_named("unsigned int");
	
	assert(results.size() > 0);
	unsigned count = 0;
	Dwarf_Off last_saw_cu = 0ul;
	bool seen_multiple_cus = false;
	for (auto i = results.begin(); i != results.end(); ++i, ++count)
	{
		/* IMPORTANT: this test needs to be statically linked! */
		Dwarf_Off cu_off = (*i).enclosing_cu().offset_here();
		if (cu_off != last_saw_cu)
		{
			if (last_saw_cu) seen_multiple_cus = true;
			last_saw_cu = cu_off;
		}
		
		if ((*i).name_here())
		{
			string name = *(*i).name_here();
			assert(name == "unsigned int");
		}
	}
	assert(seen_multiple_cus);
	assert(count > 0);
	
	// test again, after cache populated
	std::vector<iterator_base> results2
	 = r.find_all_visible_grandchildren_named("unsigned int");
	assert(results.size() == results2.size());

	return 0;
}
