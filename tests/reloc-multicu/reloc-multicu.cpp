/*
 * Unlike libdwarf, libdw does not apply relocations for ET_REL files.
 * The most damaging victim is each CU header's debug_abbrev_offset: in a multi-CU `ld -r` object, every CU after the first
 * has a non-zero abbrev offset carried by an R_X86_64_32 relocation. Left
 * unapplied, libdw matches those CUs against the first CU's abbrev table and
 * mangles all of their names and type references.
 */

#include <fstream>
#include <cassert>
#include <iostream>
#include <set>
#include <string>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>

using std::cout;
using std::endl;
using std::set;
using std::string;
using namespace dwarf;

int main(int argc, char **argv)
{
	const char *fixture = "reloc-multicu-fixture.o";
	cout << "Opening fixture " << fixture << "..." << endl;
	std::ifstream in(fixture);
	assert(in && "could not open reloc-multicu-fixture.o (built by include.mk)");
	core::root_die root(fileno(in));

	set<string> typedef_names;
	unsigned cu_count = 0;
	for (auto i = root.begin(); i != root.end(); ++i)
	{
		if (i.tag_here() == DW_TAG_compile_unit) ++cu_count;
		if (i.tag_here() != DW_TAG_typedef) continue;
		auto name = i.name_here();
		if (name) typedef_names.insert(string(name.get()));
	}

	cout << "Saw " << cu_count << " CU(s); typedef names:";
	for (auto const& n : typedef_names) cout << " " << n;
	cout << endl;

	assert(cu_count >= 2 && "fixture should contain at least two CUs");
	/* alpha_t lives in the first CU (abbrev offset 0, always parses); beta_t
	 * lives in the second CU and only parses correctly if its abbrev-offset
	 * relocation was applied. */
	assert(typedef_names.count("alpha_t")
		&& "first-CU typedef missing -- fixture not as expected");
	assert(typedef_names.count("beta_t")
		&& "second-CU typedef name mangled: ET_REL abbrev-offset relocation "
		   "was not applied");
	cout << "Both CUs parsed correctly." << endl;
	return 0;
}
