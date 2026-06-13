#include <fstream>
#include <cassert>
#include <iostream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/attr.hpp>
#include <dwarfpp/expr.hpp>

using std::cout;
using std::endl;
using namespace dwarf;
using dwarf::core::variable_die;

volatile int static_var_for_test = 0x5eed;
int *keep_alive = (int*) &static_var_for_test;

int main(int argc, char **argv)
{
	cout << "Opening " << argv[1] << "..." << endl;
	std::ifstream in(argv[1]);
	core::root_die root(fileno(in));

	unsigned resolved = 0;
	for (auto i = root.begin(); i != root.end(); ++i)
	{
		if (i.tag_here() != DW_TAG_variable
			|| !i.has_attribute_here(DW_AT_location)) continue;
		if (!i.as_a<variable_die>()->has_static_storage()) continue;

		core::Attribute a(dynamic_cast<core::Die&>(i.get_handle()), DW_AT_location);
		encap::attribute_value val(a, dynamic_cast<core::Die&>(i.get_handle()), root);
		if (!val.is_loclist()) continue;
		auto loclist = val.get_loclist();
		if (loclist.begin() == loclist.end()) continue; // optimised-out

		// a bare exprloc must be selectable at vaddr 0.
		bool threw = false;
		try { (void) loclist.loc_for_vaddr(0); }
		catch (...) { threw = true; }
		assert(!threw
			&& "static variable's location expression not resolvable at vaddr 0 "
			   "(bare-exprloc range must be [0, max), not [0,0))");
		++resolved;
	}

	cout << "Resolved locations for " << resolved << " static-storage variable(s)."
		<< endl;
	assert(resolved > 0
		&& "expected at least one static-storage variable with a location");
	return 0;
}
