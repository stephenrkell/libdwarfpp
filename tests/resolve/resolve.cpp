#undef NDEBUG // assert is part of our logic
#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>

#include <srk31/algorithm.hpp>

using std::cout; 
using std::endl;
using std::vector;
using namespace dwarf;

void f(void) __attribute__((optimize("O0")));
void f(void)
{
	struct S
	{
		int c;
		union U
		{
			float m;
			char *p;
		} u;
	} s;
}

int main(int argc, char **argv)
{
	using namespace dwarf::core; 

	// using our own debug info...
	
	std::ifstream in(argv[0]);
	assert(in);
	core::root_die r(fileno(in));
	
	// resolve some stuff, which we put in results
	vector<iterator_base> results;
	vector<string> path;
	
	// try "main" -- should be visible from root
	path = { "main" };
	results.clear();
	r.resolve_all_visible_from_root(path.begin(), path.end(), results);
	assert(results.size() == 1);
	auto main = results.at(0);
	assert(main);
	
	// try "argv" -- should be visible from main
	path = { "argv" };
	results.clear();
	r.resolve_all(main, path.begin(), path.end(), results);
	assert(results.size() >= 1);

}
