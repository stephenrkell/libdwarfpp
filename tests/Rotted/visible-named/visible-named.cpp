#include <dwarfpp/lib.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/adt.hpp>
//#include <dwarfpp/encap_graph.hpp>
#include <cstdio>
#include <cassert>
#include <set>

int
main(int argc,char*argv[])
{
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");

	// construct a dwarf::encap::file
	dwarf::encap::file encap_df(fileno(f));
	
	dwarf::encap::dieset& ds = encap_df.get_ds();
	
	// print visible named children
	auto vg_seq = ds.toplevel()->visible_grandchildren_sequence();
	for (auto i_vg = vg_seq->begin(); i_vg != vg_seq->end(); ++i_vg)
	{
		std::cout << "Visible toplevel " << (*i_vg)->summary() << std::endl; 
	}
}

