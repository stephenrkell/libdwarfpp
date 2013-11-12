#ifndef DWARFIDL_CXX_DEPENDENCY_ORDER_HPP_
#define DWARFIDL_CXX_DEPENDENCY_ORDER_HPP_

#include <set>
#include <boost/graph/graph_traits.hpp>
#include <dwarfpp/encap_graph.hpp>
#include <dwarfpp/encap_sibling_graph.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>

namespace dwarf { namespace tool { 

using std::set;
using std::string;
using std::vector;
using namespace boost;

using namespace dwarf;
using namespace dwarf::lib;

class dwarfidl_cxx_target : public cxx_target
{
	const string m_untyped_argument_typename;
	srk31::indenting_ostream& out;
public:	
	//static const vector<string> default_compiler_argv;
	dwarfidl_cxx_target(
		const string& untyped_argument_typename,
		srk31::indenting_ostream& out,
		const vector<string>& compiler_argv = default_compiler_argv(true)
	)
	 : cxx_target(compiler_argv), 
	m_untyped_argument_typename(untyped_argument_typename), 
	out(out)
	{}
	
	string get_untyped_argument_typename() { return m_untyped_argument_typename; }
	
// 	void 
// 	emit_typedef(
// 		shared_ptr<spec::type_die> p_d,
// 		const string& name
// 	)
// 	{ out << make_typedef(p_d, name); }

	void emit_all_decls(shared_ptr<spec::file_toplevel_die> p_d);
	
	void emit_forward_decls(const set<encap::basic_die *>& fds);
};

// we need a new edge iterator only
struct cpp_dependency_order;
template<typename Value = encap::attribute_value::weak_ref>
struct skip_edge_iterator
: public iterator_adaptor<skip_edge_iterator<Value>, // Derived
			typename graph_traits<encap::basic_die>::out_edge_iterator, // Base
			Value,											   // Value
			use_default, // Traversal
			Value // Reference
		> 
{ 
	typedef typename graph_traits<encap::basic_die>::out_edge_iterator Base;
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
struct cycle_handler : public boost::dfs_visitor<>
{
	/* We use this map to record the shortest path 
	 * from our back edge target 
	 * to our back edge source, i.e. the "forward" path
	 * that forms path of the cycle. */ 
 	typedef std::map<
		dwarf::encap::basic_die *, 
		std::vector<
			skip_edge_iterator<>::value_
		> 
	> PathMap;
	//PathMap& paths;
	set<encap::basic_die *>& new_forward_decls;
	vector<encap::attribute_value::weak_ref>& new_skipped_edges;
	cycle_handler(/*PathMap& paths,*/ set<encap::basic_die *>& new_forward_decls,
		vector<encap::attribute_value::weak_ref>& new_skipped_edges) 
	  : /*paths(paths),*/ new_forward_decls(new_forward_decls), new_skipped_edges(new_skipped_edges) 
	{ /*assert(paths.size() == 0);*/ }

	template <class Vertex, class Graph>
	PathMap get_bfs_paths(Vertex from, Graph& g);

	template <class Edge, class Graph>
	void print_back_edge_and_cycle(Edge e, Graph& g, PathMap& paths);
	
	template <class Edge, class Graph>
	void print_edge(Edge e, Graph& g);
	
	template <class Edge, class Graph>
	void back_edge(Edge e, Graph& g);
};
struct noop_cycle_handler : public cycle_handler
{
	template <class Edge, class Graph>
	void back_edge(Edge e, Graph& g) 
	{
		auto my_target = target(e, g);
		PathMap paths = get_bfs_paths(my_target, g);
		print_back_edge_and_cycle(e, g, paths); 
	}
	
	noop_cycle_handler(//PathMap& paths, 
		set<dwarf::encap::basic_die *>& new_forward_decls,
		vector<dwarf::encap::attribute_value::weak_ref>& new_skipped_edges)
	: cycle_handler(new_forward_decls, new_skipped_edges) {}
};
struct cpp_dependency_order
{
	set<encap::basic_die *> forward_decls;
	vector<encap::attribute_value::weak_ref> skipped_edges;
	//bool is_initialized;
	encap::basic_die* p_parent;
	typedef vector<graph_traits<encap::basic_die>::vertex_descriptor> 
		container;
	container topsorted_container;

	explicit cpp_dependency_order(encap::basic_die& parent); 
};
} }
namespace boost
{
	template<>
	struct graph_traits<dwarf::tool::cpp_dependency_order> 
	{
		// copied from encap_sibling_graph.hpp (inheritance doesn't work for some reason...)
		typedef dwarf::encap::basic_die *vertex_descriptor;
		
		static vertex_descriptor null_vertex() { return nullptr; }
		
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
	using std::pair;
	using std::make_pair;
	using std::set;
	using std::string;
	using std::vector;
	using namespace boost;

	using namespace dwarf;
	using namespace dwarf::lib;
	
	pair<
		skip_edge_iterator<>,
		skip_edge_iterator<> >  
	out_edges(
		graph_traits<cpp_dependency_order>::vertex_descriptor u, 
		const cpp_dependency_order& g);
	pair<
		graph_traits<cpp_dependency_order>::vertex_iterator,
		graph_traits<cpp_dependency_order>::vertex_iterator >  
	vertices(const cpp_dependency_order& g);
	graph_traits<cpp_dependency_order>::degree_size_type
	out_degree(
		graph_traits<cpp_dependency_order>::vertex_descriptor u,
		const cpp_dependency_order& g);
	graph_traits<cpp_dependency_order>::vertex_descriptor
	source(
		graph_traits<cpp_dependency_order>::edge_descriptor e,
		const cpp_dependency_order& g);
	graph_traits<cpp_dependency_order>::vertex_descriptor
	target(
		graph_traits<cpp_dependency_order>::edge_descriptor e,
		const cpp_dependency_order& g);
	graph_traits<cpp_dependency_order>::vertices_size_type 
	num_vertices(const cpp_dependency_order& g);

template <typename PathMap>
class bfs_path_recorder : public boost::default_bfs_visitor
{
private:
	PathMap& p;
public:
	bfs_path_recorder(PathMap& pm) : p(pm) { }

	template <typename Edge, typename Graph>
	void tree_edge(Edge e, const Graph& g) 
	{
 		typename boost::graph_traits<Graph>::vertex_descriptor 
			u = source(e, g), 
			v = target(e, g);
		
		// the shortest path to v...
		p[v]
		// ... is the shortest path to u...
		= p[u]
		// ... plus the edge from u to v
		; p.find(v)->second.push_back(e);
		// sanity check
		assert(p[v].size() == p[u].size() + 1);
	}
};
	inline std::pair<
		graph_traits<dwarf::tool::cpp_dependency_order>::out_edge_iterator,
		graph_traits<dwarf::tool::cpp_dependency_order>::out_edge_iterator >  
	out_edges(
		graph_traits<dwarf::tool::cpp_dependency_order>::vertex_descriptor u, 
		const dwarf::tool::cpp_dependency_order& g
		)
	{
		auto u_all_refs_seq = u->all_refs_dfs_seq();
		auto relevant_begin = graph_traits<dwarf::encap::basic_die>::relevant_ref_attrs_iterator(
					std::bind2nd(encap::ref_points_under, 
						const_cast<encap::basic_die *>(g.p_parent)),
					u_all_refs_seq->begin(), 
					u_all_refs_seq->end());
		auto relevant_end = graph_traits<dwarf::encap::basic_die>::relevant_ref_attrs_iterator(
					std::bind2nd(encap::ref_points_under, 
						const_cast<encap::basic_die *>(g.p_parent)),
					u_all_refs_seq->end(), 
					u_all_refs_seq->end());
					
		graph_traits<encap::basic_die>::out_edge_iterator transformed_begin(
			relevant_begin, graph_traits<encap::basic_die>::get_ref_t());
		graph_traits<encap::basic_die>::out_edge_iterator transformed_end(
			relevant_end, graph_traits<encap::basic_die>::get_ref_t());
				
		return std::make_pair(
			graph_traits<dwarf::tool::cpp_dependency_order>::out_edge_iterator(
					transformed_begin, 
					transformed_begin, 
					transformed_end,
					g),
			graph_traits<dwarf::tool::cpp_dependency_order>::out_edge_iterator(
					transformed_end, 
					transformed_begin, 
					transformed_end,
					g)
							
			);
	}
	
//	 graph_traits<dwarf::tool::cpp_dependency_order>::vertices_size_type 
//	 num_vertices(const dwarf::tool::cpp_dependency_order& g);

	inline graph_traits<dwarf::tool::cpp_dependency_order>::degree_size_type
	out_degree(
		graph_traits<dwarf::tool::cpp_dependency_order>::vertex_descriptor u,
		const dwarf::tool::cpp_dependency_order& g)
	{
		// HACK: we shouldn't really do this
		unsigned count = 0;
		dwarf::tool::cpp_dependency_order& vg = const_cast<dwarf::tool::cpp_dependency_order&>(g);
		encap::die::attribute_map::iterator attrs_end = vg.p_parent->m_attrs.end();
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

#endif
