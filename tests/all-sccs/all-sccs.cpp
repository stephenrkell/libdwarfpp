#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/abstract.hpp>
#include <dwarfpp/abstract-inl.hpp>
#include <dwarfpp/root.hpp>
#include <dwarfpp/root-inl.hpp>
#include <dwarfpp/iter.hpp>
#include <dwarfpp/iter-inl.hpp>
#include <dwarfpp/dies.hpp>
#include <dwarfpp/dies-inl.hpp>

int main(int argc, char **argv)
{
	using namespace dwarf::core;
	using std::cerr;
	using std::endl;
	
	std::ifstream in(argv[1]);
	root_die root(fileno(in));
	
	/* Compute all SCCs for all type DIEs */
	for (iterator_df<> i = root.begin(); i != root.end(); ++i)
	{
		if (i.is_a<type_die>())
		{
			auto got_scc = i.as_a<type_die>()->get_scc();
			cerr << i.summary() << " has SCC? " << (got_scc ? "yes; " : "no.");
			if (got_scc)
			{
				cerr << "it has " << got_scc->size() << " edges.";
			}
			cerr << std::endl;
		}
	}

	return 0;
}
