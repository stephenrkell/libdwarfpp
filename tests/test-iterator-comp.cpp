#include <dwarfpp/lib.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/adt.hpp>
#include <cstdio>
#include <cassert>
#include <set>

using boost::shared_ptr;
using boost::dynamic_pointer_cast;
using dwarf::spec::subprogram_die;

int
main(int argc,char*argv[])
{
	using namespace std;
	using namespace dwarf;
	
	assert(argc > 0);
	FILE* f = fopen(argv[0], "r");

	// construct a dwarf::lib::file
	lib::file df(fileno(f));
	// construct a dwarf::lib::dieset
	lib::dieset ds(df);

	auto path = vector<std::string>(1, "main");
	auto found = ds.toplevel()->visible_resolve(path.begin(), path.end());
	assert(found);
	auto subprogram = dynamic_pointer_cast<subprogram_die>(found);
	assert(subprogram);
	auto i_firstarg = subprogram->formal_parameter_children_begin();
	auto i_secondarg = i_firstarg; i_secondarg++;

	assert(i_secondarg != subprogram->formal_parameter_children_end());

	assert(subprogram->iterator_here().base().off != 
		numeric_limits<lib::Dwarf_Off>::max());
	
	cout << "main() is at offset 0x" << hex << subprogram->get_offset() << dec << endl;
	cout << "argc (name: " << *(*i_firstarg)->get_name() << ") is at offset 0x"
			<< hex << (*i_firstarg)->get_offset() << dec << endl;
	cout << "argv (name: " << *(*i_secondarg)->get_name() << ") is at offset 0x"
			<< hex << (*i_secondarg)->get_offset() << dec << endl;
	cout << "argc's parent (name: " << *(*i_firstarg)->get_parent()->get_name() 
			<< ") is at offset 0x" << hex << (*i_firstarg)->get_parent()->get_offset() << dec << endl;
	cout << "argv's parent (name: " << *(*i_secondarg)->get_parent()->get_name() 
			<< ") is at offset 0x" << hex << (*i_secondarg)->get_parent()->get_offset() << dec << endl;
	

	assert((*i_firstarg)->get_parent()->iterator_here().base() == subprogram->iterator_here().base());
	assert((*i_secondarg)->get_parent()->iterator_here().base() == subprogram->iterator_here().base());

	assert((*i_firstarg)->get_parent()->iterator_here() == subprogram->iterator_here());
	assert((*i_secondarg)->get_parent()->iterator_here() == subprogram->iterator_here());
	
	assert(i_secondarg.base().base().shares_parent_pos(i_firstarg.base().base()));
	assert(i_firstarg < i_secondarg);
	assert(!(i_firstarg >= i_secondarg));
}
