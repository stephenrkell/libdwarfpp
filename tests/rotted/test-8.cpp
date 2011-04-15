#include <boost/graph/graph_traits.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/encap_adt.hpp>
#include <dwarfpp/encap_sibling_graph.hpp>
#include <cstdio>
#include <boost/graph/graph_concepts.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/breadth_first_search.hpp>

typedef dwarf::encap::Die_encap_base Graph;

template <typename PathMap>
class bfs_path_recorder : public boost::default_bfs_visitor
{
    public:
    bfs_path_recorder(PathMap& path) : p(path) { }

    template <typename Edge, typename Graph>
    void tree_edge(Edge e, const Graph& g) 
    {
 	    typename boost::graph_traits<Graph>::vertex_descriptor 
        	u = boost::source(e, g), 
    	    v = boost::target(e, g);
	    p[v] = p[u]; p.find(v)->second.push_back(e);
    }
    private:
      PathMap& p;
};

struct cycle_detector : public boost::dfs_visitor<>
{
    cycle_detector( bool& has_cycle) 
      : _has_cycle(has_cycle) { }

 	typedef std::map<
        dwarf::encap::Die_encap_base *, 
        std::vector<
            boost::graph_traits<Graph>::edge_descriptor
        > 
    > PathMap;
    
    template <class Edge, class Graph>
    void back_edge(Edge e, Graph& g) {
        _has_cycle = true;
        assert(e.p_ds != 0);
        std::cerr << "Found a back-edge! Node at 0x" << std::hex << e.referencing_off << std::dec;
        dwarf::encap::Die_encap_base *source = boost::source(e, g);
        	//&dynamic_cast<dwarf::encap::Die_encap_base&>(*(*(e.p_ds))[e.referencing_off]);
        dwarf::encap::Die_encap_base *target = boost::target(e, g);
        	//&dynamic_cast<dwarf::encap::Die_encap_base&>(*(*(e.p_ds))[e.off]);
         std::cerr << ", name " <<
            		( source->has_attr(DW_AT_name) ?
                      source->get_attr(DW_AT_name).get_string() : "(anonymous)") 
            	<< " depends on node at 0x" << std::hex << e.off << std::dec << ", name " <<
                	( target->has_attr(DW_AT_name) ?
                      target->get_attr(DW_AT_name).get_string() : "(anonymous)") 
                << " owing to attribute " << e.p_ds->get_spec().attr_lookup(e.referencing_attr)
        // now print the corresponding forward path
                <<  ", while a reverse path exists: ";
        // by doing BFS from the target node...
        PathMap paths;
        std::map<
    	    dwarf::encap::Die_encap_base *, 
    	    typename boost::default_color_type
        > underlying_bfs_node_color_map;
	    auto bfs_color_map = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
        	underlying_bfs_node_color_map
        ); 
        bfs_path_recorder<PathMap> vis(paths);
        auto visitor = boost::visitor(vis).color_map(bfs_color_map);
        boost::breadth_first_search(g, boost::target(e, g), visitor);
        /// and printing the reverse path
        if (paths.find(source) != paths.end())
        {
            for (PathMap::mapped_type::iterator i_e = paths.find(source)->second.begin();
        		    i_e != paths.find(source)->second.end();
                    i_e++)
            {
        	    auto e = *i_e;
                assert(e.p_ds != 0);
                dwarf::encap::Die_encap_base *source = 
        	        &dynamic_cast<dwarf::encap::Die_encap_base&>(*(*(e.p_ds))[e.referencing_off]);
                dwarf::encap::Die_encap_base *target = 
        	        &dynamic_cast<dwarf::encap::Die_encap_base&>(*(*(e.p_ds))[e.off]);
                auto hop = target;

        	    if (i_e != paths.find(target)->second.begin()) std::cerr << "---> " << std::endl;
        	    std::cerr << *hop << std::endl;
            }
	    }
    }
protected:
    bool& _has_cycle;
};

int
main(int argc,char*argv[])
{
	assert(argc > 1); 
 	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	dwarf::encap::file df(fileno(f));
   
    // concept check
    boost::function_requires< boost::IncidenceGraphConcept<Graph> >();
    boost::function_requires< boost::VertexListGraphConcept<Graph> >();    
    
	// print dependencies among first CU's toplevel elements
    dwarf::encap::Die_encap_base& parent = 
    	**df.get_ds().all_compile_units().compile_units_begin();

    std::map<
    	boost::graph_traits<Graph>::vertex_descriptor, 
    	boost::default_color_type
    > underlying_dfs_node_color_map, underlying_topsort_node_color_map;
        
    // print the dependency edges
    std::pair<
        boost::graph_traits<dwarf::encap::Die_encap_base>::vertex_iterator,
        boost::graph_traits<dwarf::encap::Die_encap_base>::vertex_iterator >  
	vertices = boost::vertices(parent);
    for (boost::graph_traits<Graph>::vertex_iterator i_vertex = vertices.first;
    		i_vertex !=  vertices.second;
            i_vertex++)
    {
    	std::cout << "Considering node " << 
        	((*i_vertex)->get_name() ? *(*i_vertex)->get_name() : "anonymous") << std::endl;
    	std::pair<
        	boost::graph_traits<dwarf::encap::Die_encap_base>::out_edge_iterator,
	        boost::graph_traits<dwarf::encap::Die_encap_base>::out_edge_iterator >  
	    out_edges = boost::out_edges(*i_vertex, parent);
        for (boost::graph_traits<Graph>::out_edge_iterator i_edge = out_edges.first;
        		i_edge != out_edges.second;
                i_edge++)
        {
        	std::cout << "Node ";
            if ((*i_vertex)->get_name()) std::cout << *((*i_vertex)->get_name()) << "@";
            else std::cout << "anonymous@";
            std::cout << std::hex << ((*i_vertex)->get_offset()) << std::dec;
            std::cout << " depends on node ";
            if (boost::target(*i_edge, parent)->has_attr(DW_AT_name)) std::cout << 
                (*boost::target(*i_edge, parent))[DW_AT_name].get_string() << "@";
            else std::cout << "anonymous@";
            std::cout << std::hex << boost::target(*i_edge, parent)->get_offset();
            std::cout << " owing to attribute " << i_edge->p_ds->get_spec().attr_lookup(i_edge->referencing_attr)
            		<< " in DIE ";
            if ((*(i_edge->p_ds))[i_edge->referencing_off]->has_attr(DW_AT_name))
            	std::cout << (*(i_edge->p_ds))[i_edge->referencing_off]->get_attr(DW_AT_name).get_string() << "@";
            else std::cout << "anonymous@";
            std::cout << std::hex << i_edge->referencing_off
                          << std::endl;
        }
    }

	// check for cycles
    bool has_cycle = false;
    cycle_detector vis(has_cycle);
    auto dfs_color_map = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
        	underlying_dfs_node_color_map
        );
    auto visitor = boost::visitor(vis).color_map(dfs_color_map);
    boost::depth_first_search(parent, visitor);
    if(!has_cycle)
    {
	    typedef std::vector<boost::graph_traits<Graph>::vertex_descriptor>
    	    container;
        container c;
        auto topsort_color_map = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
        	    underlying_topsort_node_color_map
            );
        auto named_params = boost::color_map(topsort_color_map);
        boost::topological_sort(parent, std::back_inserter(c), named_params);
	}
}
