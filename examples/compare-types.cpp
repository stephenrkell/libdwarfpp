/* Test program for libdwarfpp. */

#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <sstream>
#include <dwarfpp/lib.hpp>

using std::vector;
using std::string;
using std::cout;
using std::cin;
using std::cerr;
using std::endl;
using std::set;
using std::pair;
using namespace dwarf::lib;
using namespace dwarf::core;

int main(int argc, char **argv)
{
	// open the file passed in on the command-line
	// followed by two offsets
	assert(argc > 3);
	FILE* f = fopen(argv[1], "r");
	
	root_die r(fileno(f));
	
	Dwarf_Off off1;
	Dwarf_Off off2;
	std::istringstream off1str(argv[2]);
	std::istringstream off2str(argv[3]);
	off1str >> std::hex >> off1;
	if (!off1str) { cerr << "Need an offset; got " << argv[2] << endl; return 1; }
	off2str >> std::hex >> off2;
	if (!off2str) { cerr << "Need an offset; got " << argv[3] << endl; return 1; }
	
	auto d1 = r.pos(off1);
	auto d2 = r.pos(off2);
	if (!d1) { cerr << "Not a valid DIE offset: " << argv[2] << endl; return 1; }
	if (!d2) { cerr << "Not a valid DIE offset: " << argv[3] << endl; return 1; }
	if (!d1.is_a<type_die>()) { cerr << "Valid DIE but not a type DIE: " << argv[2] << endl; return 1; }
	if (!d2.is_a<type_die>()) { cerr << "Valid DIE but not a type DIE: " << argv[3] << endl; return 1; }
	
	string reason;
	type_die::equal_result_t res = d1.as_a<type_die>()->equal(
		d2.as_a<type_die>(),
		set< pair< iterator_df<type_die>, iterator_df<type_die> > >(),
		opt<string&>(reason)
	);
	cout << "Comparison of " << d1 << " and " << d2 << " returned ";
	if (res == type_die::EQUAL)
	{
		cout << "EQUAL";
	}
	else if (res == type_die::UNEQUAL)
	{
		cout << "UNEQUAL for reason " << reason;
	}
	else cout << "something it shouldn't have";
	cout << endl;
	return 0;
}

