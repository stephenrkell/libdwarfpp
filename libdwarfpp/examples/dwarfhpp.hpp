#include <boost/graph/graph_traits.hpp>
#include <dwarfpp/encap_adt.hpp>
#include <dwarfpp/encap_graph.hpp>
#include <dwarfpp/encap_sibling_graph.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>

using namespace boost;

using namespace dwarf;
using namespace dwarf::lib;

namespace dwarf { namespace tool { 
// we need a new edge iterator only
struct cpp_dependency_order;
template<typename Value = dwarf::encap::attribute_value::weak_ref>
struct skip_edge_iterator
: public boost::iterator_adaptor<skip_edge_iterator<Value>, // Derived
            typename boost::graph_traits<dwarf::encap::Die_encap_base>::out_edge_iterator, // Base
            Value,											   // Value
            boost::use_default, // Traversal
            Value // Reference
        > 
{ 
	typedef  typename boost::graph_traits<dwarf::encap::Die_encap_base>::out_edge_iterator Base;
    typedef Value value_;
	const cpp_dependency_order *p_deps;
    Base m_begin;
    Base m_end;
    skip_edge_iterator(Base p, Base begin, Base end, const cpp_dependency_order& deps);
private:
    bool is_skipped(Base b);
public:
    skip_edge_iterator() {}
    void increment(); 
    void decrement(); 
};
struct cycle_detector : public boost::dfs_visitor<>
{
 	typedef std::map<
        dwarf::encap::Die_encap_base *, 
        std::vector<
            skip_edge_iterator<>::value_
        > 
    > PathMap;
    PathMap& paths;
	std::vector<dwarf::encap::Die_encap_base *>& new_forward_decls;
    std::vector<dwarf::encap::attribute_value::weak_ref>& new_skipped_edges;
    cycle_detector(PathMap& paths, std::vector<dwarf::encap::Die_encap_base *>& new_forward_decls,
    	std::vector<dwarf::encap::attribute_value::weak_ref>& new_skipped_edges) 
      : paths(paths), new_forward_decls(new_forward_decls), new_skipped_edges(new_skipped_edges) { }

    template <class Edge, class Graph>
    void back_edge(Edge e, Graph& g);
};
struct cpp_dependency_order
{
	std::vector<dwarf::encap::Die_encap_base *> forward_decls;
    std::vector<dwarf::encap::attribute_value::weak_ref> skipped_edges;
	//bool is_initialized;
    dwarf::encap::Die_encap_base* p_parent;
    typedef std::vector<boost::graph_traits<dwarf::encap::Die_encap_base>::vertex_descriptor> 
    	container;
    container topsorted_container;

    explicit cpp_dependency_order(dwarf::encap::Die_encap_base& parent); 
};
} }
namespace boost
{
    template<>
	struct graph_traits<dwarf::tool::cpp_dependency_order> 
    {
    	// copied from encap_sibling_graph.hpp (inheritance doesn't work for some reason...)
        typedef dwarf::encap::Die_encap_base *vertex_descriptor;
        typedef dwarf::encap::attribute_value::weak_ref edge_descriptor;

		typedef dwarf::encap::die_base_ptr_iterator vertex_iterator;
        
        typedef directed_tag directed_category;
        typedef allow_parallel_edge_tag edge_parallel_category;

        struct traversal_tag :
          public virtual vertex_list_graph_tag,
          public virtual incidence_graph_tag { };
        typedef traversal_tag traversal_category;
        
        typedef /*dwarf::encap::dieset::size_type*/unsigned vertices_size_type;
        typedef dwarf::encap::die::attribute_map::size_type edges_size_type;
        typedef dwarf::encap::die::attribute_map::size_type degree_size_type;

    	typedef dwarf::tool::skip_edge_iterator<> out_edge_iterator;
    };
}

namespace dwarf { namespace tool {
    std::pair<
        skip_edge_iterator<>,
        skip_edge_iterator<> >  
    out_edges(
        boost::graph_traits<cpp_dependency_order>::vertex_descriptor u, 
        const cpp_dependency_order& g);
    std::pair<
        boost::graph_traits<cpp_dependency_order>::vertex_iterator,
        boost::graph_traits<cpp_dependency_order>::vertex_iterator >  
	vertices(const cpp_dependency_order& g);
	boost::graph_traits<cpp_dependency_order>::degree_size_type
    out_degree(
    	boost::graph_traits<cpp_dependency_order>::vertex_descriptor u,
        const cpp_dependency_order& g);
    boost::graph_traits<cpp_dependency_order>::vertex_descriptor
    source(
        boost::graph_traits<cpp_dependency_order>::edge_descriptor e,
        const cpp_dependency_order& g);
    boost::graph_traits<cpp_dependency_order>::vertex_descriptor
    target(
        boost::graph_traits<cpp_dependency_order>::edge_descriptor e,
        const cpp_dependency_order& g);
	boost::graph_traits<cpp_dependency_order>::vertices_size_type 
    num_vertices(const cpp_dependency_order& g);

template <typename PathMap>
class bfs_path_recorder : public boost::default_bfs_visitor
{
    public:
    bfs_path_recorder(PathMap& path) : p(path) { }

    template <typename Edge, typename Graph>
    void tree_edge(Edge e, const Graph& g) 
    {
 	    typename boost::graph_traits<Graph>::vertex_descriptor 
        	u = source(e, g), 
    	    v = target(e, g);
	    p[v] = p[u]; p.find(v)->second.push_back(e);
    }
    private:
      PathMap& p;
};
// struct cpp_dependency_order
// {
// 	std::vector<dwarf::encap::Die_encap_base *> forward_decls;
//     // std::vector<dwarf::encap::attribute_value::weak_ref> retained_edges;
// 
//     dwarf::encap::Die_encap_base *p_parent;
//     cycle_detector::PathMap paths;
// 	std::vector<dwarf::encap::attribute_value::weak_ref> skipped_edges;
//     typedef std::vector<boost::graph_traits<dwarf::encap::Die_encap_base>::vertex_descriptor> 
//     	container;
//  
//  	cycle_detector cycle_det;
// 
//     container topsorted_container;
//     
//     cpp_dependency_order(dwarf::encap::Die_encap_base& parent); 
// };
//     template<>
// 	struct graph_traits<dwarf::tool::cpp_dependency_order> 
//     {
//     	// copied from encap_sibling_graph.hpp (inheritance doesn't work for some reason...)
//         typedef dwarf::encap::Die_encap_base *vertex_descriptor;
//         typedef dwarf::encap::attribute_value::weak_ref edge_descriptor;
// 
// 		typedef dwarf::encap::die_base_ptr_iterator vertex_iterator;
//         
//         typedef directed_tag directed_category;
//         typedef allow_parallel_edge_tag edge_parallel_category;
// 
//         struct traversal_tag :
//           public virtual vertex_list_graph_tag,
//           public virtual incidence_graph_tag { };
//         typedef traversal_tag traversal_category;
//         
//         typedef dwarf::encap::dieset::size_type vertices_size_type;
//         typedef dwarf::encap::die::attribute_map::size_type edges_size_type;
//         typedef dwarf::encap::die::attribute_map::size_type degree_size_type;
// 
//     	typedef dwarf::tool::skip_edge_iterator<> out_edge_iterator;
//     };
    
// 	std::pair<
//         dwarf::encap::die_base_ptr_iterator,
//         dwarf::encap::die_base_ptr_iterator >  
// 	vertices(const dwarf::tool::cpp_dependency_order& g);
//     
    inline std::pair<
        graph_traits<dwarf::tool::cpp_dependency_order>::out_edge_iterator,
        graph_traits<dwarf::tool::cpp_dependency_order>::out_edge_iterator >  
    out_edges(
        graph_traits<dwarf::tool::cpp_dependency_order>::vertex_descriptor u, 
        const dwarf::tool::cpp_dependency_order& g
        )
    {
    	/*auto sibling_deps_begin = dwarf::encap::sibling_dep_edge_iterator<>(
            	    const_cast<dwarf::encap::Die_encap_base&>(*g.p_parent),
                    const_cast<dwarf::encap::Die_encap_base&>(*u)
                );
        auto sibling_deps_end = dwarf::encap::sibling_dep_edge_iterator<>(
                    const_cast<dwarf::encap::Die_encap_base&>(*u).m_attrs.end(), 
                    const_cast<dwarf::encap::Die_encap_base&>(*g.p_parent),
                    const_cast<dwarf::encap::Die_encap_base&>(*u)
                );*/
                
        auto relevant_begin = graph_traits<dwarf::encap::Die_encap_base>::relevant_ref_attrs_iterator(
        			std::bind2nd(dwarf::encap::ref_points_under, 
                    	const_cast<dwarf::encap::Die_encap_base *>(g.p_parent)),
        	        u->all_refs_dfs_begin(), 
                    u->all_refs_dfs_end());
        auto relevant_end = graph_traits<dwarf::encap::Die_encap_base>::relevant_ref_attrs_iterator(
        			std::bind2nd(dwarf::encap::ref_points_under, 
                    	const_cast<dwarf::encap::Die_encap_base *>(g.p_parent)),
        	        u->all_refs_dfs_end(), 
                    u->all_refs_dfs_end());
                    
        graph_traits<dwarf::encap::Die_encap_base>::out_edge_iterator transformed_begin(
        	relevant_begin, graph_traits<dwarf::encap::Die_encap_base>::get_ref_t());
        graph_traits<dwarf::encap::Die_encap_base>::out_edge_iterator transformed_end(
        	relevant_end, graph_traits<dwarf::encap::Die_encap_base>::get_ref_t());
                
    	return std::make_pair(
        	/*graph_traits<dwarf::tool::cpp_dependency_order>::out_edge_iterator( 
            	sibling_deps_begin,
                sibling_deps_begin,
                sibling_deps_end,
                g)*/graph_traits<dwarf::tool::cpp_dependency_order>::out_edge_iterator(
                	transformed_begin, 
                    transformed_begin, 
                    transformed_end,
                    g),
            /*graph_traits<dwarf::tool::cpp_dependency_order>::out_edge_iterator(
            	sibling_deps_end,
                sibling_deps_begin,
                sibling_deps_end,
				g)*/ graph_traits<dwarf::tool::cpp_dependency_order>::out_edge_iterator(
                	transformed_end, 
                    transformed_begin, 
                    transformed_end,
                    g)
                            
            );
    }
    
//     graph_traits<dwarf::tool::cpp_dependency_order>::vertices_size_type 
//     num_vertices(const dwarf::tool::cpp_dependency_order& g);

    inline graph_traits<dwarf::tool::cpp_dependency_order>::degree_size_type
    out_degree(
    	graph_traits<dwarf::tool::cpp_dependency_order>::vertex_descriptor u,
        const dwarf::tool::cpp_dependency_order& g)
    {
    	// HACK: we shouldn't really do this
        unsigned count = 0;
        dwarf::tool::cpp_dependency_order& vg = const_cast<dwarf::tool::cpp_dependency_order&>(g);
        dwarf::encap::die::attribute_map::iterator attrs_end = vg.p_parent->m_attrs.end();
        for (dwarf::tool::skip_edge_iterator<> i = out_edges(u, g).first;
        		i != out_edges(u, g).second;
                i++) count++;
        return count;
    }
    inline graph_traits<dwarf::tool::cpp_dependency_order>::vertex_descriptor
    source(
        graph_traits<dwarf::tool::cpp_dependency_order>::edge_descriptor e,
        const dwarf::tool::cpp_dependency_order& g)
    {
        return boost::source(e, *g.p_parent);
    }
    inline graph_traits<dwarf::tool::cpp_dependency_order>::vertex_descriptor
    target(
        graph_traits<dwarf::tool::cpp_dependency_order>::edge_descriptor e,
        const dwarf::tool::cpp_dependency_order& g)
    {
    	// project the edge's target up to a child of the patriarch
        return boost::target(e, *g.p_parent);
    }
	inline std::pair<
        graph_traits<dwarf::tool::cpp_dependency_order>::vertex_iterator,
        graph_traits<dwarf::tool::cpp_dependency_order>::vertex_iterator >  
	vertices(const dwarf::tool::cpp_dependency_order& g)
    {
    	return boost::vertices(*g.p_parent);
	}    
    inline graph_traits<dwarf::tool::cpp_dependency_order>::vertices_size_type 
    num_vertices(const dwarf::tool::cpp_dependency_order& g)
    {
    	return boost::num_vertices(*g.p_parent);
    }
} }
