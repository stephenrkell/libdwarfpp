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

/* For case 1. */
struct cycle1
{
	cycle1 *next;
} dummy1;

/* For case 2. */
struct cycle2
{
	void (*fp)(cycle2 *arg);
} dummy2;

int main(int argc, char **argv)
{
	using namespace dwarf::core;
	using std::cerr;
	using std::endl;
	
	std::ifstream in(argv[0]);
	root_die root(fileno(in));
	auto cu = root.begin(); ++cu;
	
	/* Case 1. We created a simple 2-cycle:
	 * struct -> ptr -> struct.
	 * Each type in it should have the same SCC. */
	cerr << "* case 1" << std::endl;
	{
		iterator_df<type_die> type1_1 = cu.named_child("cycle1");
		assert(type1_1);
		assert(type1_1.is_a<with_data_members_die>());
		cerr << "type1_1 has abstract name: "; type1_1->print_abstract_name(cerr) << endl;
		iterator_df<type_die> type1_2 = type1_1.children().subseq_of<data_member_die>().first->get_type();
		assert(type1_2);
		assert(type1_2.is_a<pointer_type_die>());
		cerr << "type2 has abstract name: "; type1_2->print_abstract_name(cerr) << endl;
		auto scc1_1 = type1_1->get_scc();
		auto scc1_2 = type1_2->get_scc();
		assert(scc1_1 == scc1_2);
		/* The SCC should contain two edges: from the struct to the pointer (reason: member)
		 * and from the pointer to the struct (reason: pointer). */
		assert(scc1_1->size() == 2);
	}
	
	cerr << "* case 2" << std::endl;
	{
		/* Case 2. We created a simple 4-cycle:
		 *    struct -> ptr-to-subroutine -> subroutine -> ptr-to-struct -> struct.
		 * The same properties as above should hold, but also, we now do 
		 * a perf check: we should have cached the SCC in each type_die, so that
		 * the second get_scc() hits the cache. In fact we should use make_shared
		 * to store a single copy of the SCC. When we're building the SCC by running
		 * through the cycles until a fixed point, if we see an SCC in any of the DIEs
		 * we're including, we can skip to the end. */
		iterator_df<type_die> type2_1 = cu.named_child("cycle2");
		assert(type2_1);
		assert(type2_1.is_a<with_data_members_die>());
		cerr << "type2_1 has abstract name: "; type2_1->print_abstract_name(cerr) << endl;
		auto scc2_1 = type2_1->get_scc();
		assert(scc2_1->size() == 4);
		iterator_df<type_die> type2_2 = type2_1.children().subseq_of<data_member_die>().first->get_type();
		assert(type2_2->opt_cached_scc); // we have something cached
		assert(*type2_2->opt_cached_scc); // we have a non-null SCC cached
		assert(*type2_1->opt_cached_scc == *type2_2->opt_cached_scc); // we are sharing correctly
	}
	// case 3: test short-circuiting of caching logic
	

	return 0;
}
