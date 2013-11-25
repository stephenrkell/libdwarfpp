#include <dwarfpp/lib.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/adt.hpp>
//#include <dwarfpp/encap_graph.hpp>
#include <cstdio>
#include <cassert>
#include <set>

using std::cerr;
using std::endl;
using std::string;
using std::vector;

int
main(int argc,char*argv[])
{
	FILE* f = fopen("../../src/libdwarfpp.so", "r");
	assert(f);

	// construct a dwarf::encap::file
	dwarf::lib::file df(fileno(f));
	dwarf::lib::dieset ds(df);
	
	// print visible named children
	cerr << "Resolving Elf... should be slow." << endl;
	vector<string> name_parts(1, "Elf");
	auto resolved = ds.toplevel()->resolve_visible(name_parts.begin(), name_parts.end());
	assert(resolved);
	cerr << "Done." << endl;
	cerr << "Resolving Elf again... should be faster." << endl;
	auto resolved_again = ds.toplevel()->resolve_visible(name_parts.begin(), name_parts.end());
	assert(resolved_again);
	cerr << "Done." << endl;

	cerr << "Resolving Elf32_Shdr... should be slowish." << endl;
	vector<string> name_parts2(1, "Elf32_Shdr");
	auto resolved2 = ds.toplevel()->resolve_visible(name_parts2.begin(), name_parts2.end());
	assert(resolved2);
	cerr << "Done." << endl;
	cerr << "Resolving Elf32_Shdr again... should be faster." << endl;
	auto resolved2_again = ds.toplevel()->resolve_visible(name_parts2.begin(), name_parts2.end());
	assert(resolved2_again);
	cerr << "Done." << endl;
	
	return 0;
}

