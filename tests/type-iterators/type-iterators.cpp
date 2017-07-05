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

	std::ifstream in(argv[0]);
	core::root_die root(fileno(in));
	
	/* We want to use type_iterator_df and walk_type to iterate over
	 * a complex type_die and check that the same sequence comes out.
	 * Which type_die do we use? Why type_iterator_df, ideally. */

	auto cu = root.begin(); ++cu;
	auto ns1_die = cu.named_child("dwarf"); assert(ns1_die);
	auto ns2_die = ns1_die.named_child("core"); assert(ns2_die);
	auto type_iter_die = ns2_die.named_child("type_iterator_df");
	assert(type_iter_die);
	
	std::vector<pair<Dwarf_Off, Dwarf_Off> > seen_via_walk_type;
	std::vector<pair<Dwarf_Off, Dwarf_Off> > seen_via_type_iterator;
	std::cerr << "Walking the old way." << std::endl;
	walk_type(type_iter_die.as_a<type_die>(),
		iterator_base::END,
		/* pre_f */ [&seen_via_walk_type](iterator_df<type_die> t, iterator_df<program_element_die> reason) {
			std::cerr << "Type " << t.summary() << ", reason: " << (reason ? reason.summary() : "(no reason)") << std::endl;
			seen_via_walk_type.push_back(make_pair(t.offset_here(), reason ? reason.offset_here() : (Dwarf_Off)-1));
			return true; // keep going
		},
		/* post_f */ [](iterator_df<type_die> t, iterator_df<program_element_die> reason) -> void {
			return;
		}
	);
	std::cerr << "==================================================" << std::endl;
	std::cerr << "Walking the new way." << std::endl;
	for (dwarf::core::type_iterator_df i = type_iter_die;
		i;
		++i)
	{
		seen_via_type_iterator.push_back(make_pair(i.offset_here(), i.reason() ? i.reason().offset_here() : (Dwarf_Off)-1));
		std::cerr << "Type " << i.summary() << ", reason: " << (i.reason() ? i.reason().summary() : "(no reason)") << std::endl;
	}
	std::cerr << "==================================================" << std::endl;
	assert(seen_via_walk_type == seen_via_type_iterator);

	return 0;
}
