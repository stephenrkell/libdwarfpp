#include <boost/graph/graph_concepts.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/encap_graph.hpp>
#include <cstdio>

struct cycle_detector : public boost::dfs_visitor<>
{
    cycle_detector( bool& has_cycle) 
      : _has_cycle(has_cycle) { }

    template <class Edge, class Graph>
    void back_edge(Edge, Graph&) {
        _has_cycle = true;
    }
protected:
    bool& _has_cycle;
};

int
main(int argc,char*argv[])
{
    typedef dwarf::encap::dieset Graph;
    //boost::function_requires< boost::VertexListGraphConcept<Graph> >();
    boost::function_requires< boost::IncidenceGraphConcept<Graph> >();
    //function_requires< BidirectionalGraphConcept<Graph> >();
    //function_requires< MutableGraphConcept<Graph> >();

	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	dwarf::encap::file df(fileno(f));
    
    // now try finding cycles in the graph
    bool has_cycle = false;
    
    std::map<
    	boost::graph_traits<dwarf::encap::dieset>::vertex_descriptor, 
    	boost::default_color_type
    > underlying_node_color_map;
    auto color_map = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
        	underlying_node_color_map
        );
    cycle_detector vis(has_cycle);
    auto root_vertex = *df.get_ds().find(0UL);
    auto visitor = boost::visitor(vis).color_map(color_map).root_vertex(root_vertex);
    
    boost::depth_first_search(df.get_ds(), visitor);
    std::cout << "The graph has a cycle? " << has_cycle << std::endl;
    
    for (auto i_vertex = boost::vertices(df.get_ds()).first; 
    	i_vertex != boost::vertices(df.get_ds()).second;
        i_vertex++)
    {
    	if (boost::out_degree(*i_vertex, df.get_ds()) == 0) continue;
        
		std::cout << "Found vertex with nonzero out degree: " << *(i_vertex->second) << std::endl;
    	for (auto i_edge = boost::out_edges(*i_vertex, df.get_ds()).first;
        	i_edge != boost::out_edges(*i_vertex, df.get_ds()).second;
            i_edge++)
        {
        	std::cout << "Found an edge going to " << *(boost::target(*i_edge, df.get_ds()).second)
            	<< std::endl;
        }

    }
    
    return 0;
}

