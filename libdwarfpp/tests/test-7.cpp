#include <boost/graph/graph_concepts.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/encap_graph.hpp>
#include <cstdio>


int
main(int argc,char*argv[])
{
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	dwarf::encap::file df(fileno(f));
    
    unsigned die_count = 0;
    
    dwarf::encap::die_off_list forward_order;
    dwarf::encap::die_off_list backward_order;
    
    for (auto i = df.get_ds()[0UL]->depthfirst_begin();
    	i != df.get_ds()[0UL]->depthfirst_end();
        i++)
    {
    	std::cout << *i;
        forward_order.push_back(i->get_offset());
        die_count++;
    }
    std::cout << "Visited " << die_count << " DIEs." << std::endl;

	auto start_i = df.get_ds()[0UL]->depthfirst_end();
	// now try the reverse direction
    for (auto i = start_i;
    	i != df.get_ds()[0UL]->depthfirst_begin() && (i--, true); )
    {
        backward_order.push_back(i->get_offset());
    }
    
    // check that the orders are mutual reverses
	std::reverse(backward_order.begin(), backward_order.end());
    assert(backward_order == forward_order);
}
