#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <sstream>
#include <cmath>

#include <boost/algorithm/string.hpp>

#include <indenting_ostream.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/cxx_model.hpp>
#include "dwarfhpp.hpp"
#include <srk31/algorithm.hpp>

using namespace srk31;
using namespace dwarf;
using namespace dwarf::lib;
using std::vector;
using std::map;
using std::string;
using std::cout;
using std::cerr;
using std::endl;
using std::hex;
using std::dec;
using std::ostringstream;
using std::stack;
using std::deque;
using boost::optional;
using boost::shared_ptr;
using boost::dynamic_pointer_cast;
using namespace dwarf::lib;

/* FIXME: this file is a mess because it was written in a direct dwarf::encap::die& style 
 * and pieces of it have since migrated to use shared_ptr, whereas others haven't. 
 * I should fix it sometime. */
 
namespace dwarf { namespace tool {

class dwarfhpp_cxx_target : public cxx_target
{
	const string m_untyped_argument_typename;
	srk31::indenting_ostream& out;
public:	
	dwarfhpp_cxx_target(
		const string& untyped_argument_typename,
		srk31::indenting_ostream& out
	)
	 : cxx_target((vector<string>) { "g++",
		"-fno-eliminate-unused-debug-types",
		"-fno-eliminate-unused-debug-symbols"
	}), 
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
	
	void emit_forward_decls(vector<encap::basic_die *> fds);
};

void dwarfhpp_cxx_target::emit_forward_decls(vector<encap::basic_die *> fds)
{
	cout << "// begin a group of forward decls" << endl;
	for (auto i = fds.begin(); i != fds.end(); i++)
	{
		assert((*i)->get_tag() == DW_TAG_structure_type
			&& (*i)->get_name());
			
		std::cout << "struct " << protect_ident(*(*i)->get_name()) 
			<< "; // forward decl" << std::endl;
	}
	cout << "// end a group of forward decls" << std::endl;
}

// this is the base case
void dwarfhpp_cxx_target::emit_all_decls(shared_ptr<spec::file_toplevel_die> p_d)
{
	/* Basic operation:
	 * For each DIE that is something we can put into a header,
	 * we emit a declaration.
	 *
	 * For DIEs that contain other DIEs, we emit the parent as
	 * an enclosing block, and recursively emit the children.
	 * This holds for namespaces, struct/class/union types,
	 * functions (where the "enclosed block" is the bracketed
	 * part of the signature) and probably more.
	 *
	 * For each declaration we emit, we should check that the
	 * ABI a C++ compiler will generate from our declaration is
	 * compatible with the ABI that our debugging info encodes.
	 * This means that for functions, we check the symbol name.
	 * We should also check base types (encoding) and structured
	 * types (layout). For now we just use alignment annotations.
	 *
	 * Complication! We need to emit things in the right order.
	 * Ordering is constrained by dependency: we assume any 
	 * DIE reference forms a dependency. We consider one enclosing
	 * layer at a time; any reference *originating* or *terminating*
	 * in an *enclosed* (i.e. child) DIE is associated with its
	 * parent in our current layer. From this we build a dependency
	 * graph and top-sort it to give us the order in which we need
	 * to emit our declarations.
	 */
	 
	auto i_d = p_d->iterator_here();
		
	for (abstract_dieset::iterator cu = p_d->children_begin(); 
				cu != p_d->children_end(); ++cu)
	{ 
		cpp_dependency_order order(*dynamic_pointer_cast<encap::basic_die>(*cu)); 
		emit_forward_decls(order.forward_decls); 
		for (cpp_dependency_order::container::iterator i = order.topsorted_container.begin(); 
				i != order.topsorted_container.end(); 
				i++) 
		{ 
			//if (!spec::file_toplevel_die::is_visible()(p_d)) continue; 
			emit_model<0>( 
				out,
				dynamic_pointer_cast<encap::basic_die>((*i)->shared_from_this())->iterator_here()
			); 
		} 
	}
}

} } // end namespace dwarf::tool

#include <boost/graph/graph_concepts.hpp>   
int main(int argc, char **argv)
{
	boost::function_requires< boost::IncidenceGraphConcept<dwarf::tool::cpp_dependency_order> >();
	boost::function_requires< boost::VertexListGraphConcept<dwarf::tool::cpp_dependency_order> >();	

	// open the file passed in on the command-line
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	dwarf::lib::file df(fileno(f));
	
	// encapsulate the DIEs
	dwarf::encap::file def(fileno(f));	
	auto all_cus = def.ds().toplevel();

	indenting_ostream& s = srk31::indenting_cout;
	dwarf::tool::dwarfhpp_cxx_target target(" ::cake::unspecified_wordsize_type", s);
	
	target.emit_all_decls(all_cus);

	return 0;
}

namespace dwarf { namespace tool {
template <typename Value>
skip_edge_iterator<Value>::skip_edge_iterator(
	Base p, Base begin, Base end, const cpp_dependency_order& deps)
		: skip_edge_iterator::iterator_adaptor_(p), p_deps(&deps), m_begin(begin), m_end(end) 
{
	// adjust m_end so that it points one past the last *non-skipped* edge
	while(m_end != m_begin) 
	{ 
		Base one_prev = m_end; one_prev--;
		if (!is_skipped(one_prev)) 
		{
			// we found a non-skipped edge
			break; 
		}
		else 
		{
			if (this->base() == m_end) this->base_reference()--;
			--m_end;
		}
	}
	// adjust m_begin so that it points to the first non-skipped edge
	while(m_begin != m_end && is_skipped(m_begin)) 
	{
		if (this->base() == m_begin) this->base_reference()++;
		m_begin++;
	}
	// adjust base so that it points to a non-skipped edge
	if (this->base() != m_end && is_skipped(this->base())) increment();
}
template <typename Value>
bool skip_edge_iterator<Value>::is_skipped(Base b)
{
	return b != m_end && std::find(p_deps->skipped_edges.begin(),
		p_deps->skipped_edges.end(),
		*b)
		!= p_deps->skipped_edges.end();
}
template <typename Value>
void skip_edge_iterator<Value>::increment()
{
	//Base old_base;
	do
	{
		/*old_base = */this->base_reference()++;
	} while (//this->base() != old_base &&
		is_skipped(this->base()));
}

template <typename Value>
void skip_edge_iterator<Value>::decrement()
{
	//Base old_base;
	do
	{
		/*old_base = */this->base_reference()--;
	} while (//this->base() != old_base &&
		is_skipped(this->base()));
}

template <class Vertex, class Graph>
cycle_handler::PathMap cycle_handler::get_bfs_paths(Vertex from, Graph& g)
{
	PathMap paths;
	std::map<
		dwarf::encap::basic_die *, 
		typename boost::default_color_type
	> underlying_bfs_node_color_map;
	auto bfs_color_map = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
		underlying_bfs_node_color_map
	); 
	bfs_path_recorder<PathMap> vis(paths);
	auto visitor = boost::visitor(vis).color_map(bfs_color_map);
	boost::breadth_first_search(g, from, visitor);
	return paths;
}

template <class Edge, class Graph>
void cycle_handler::print_back_edge_and_cycle(Edge e, Graph& g, PathMap& paths)
{
	encap::basic_die *my_source = source(e, g);
	encap::basic_die *my_target = target(e, g);
	assert(e.p_ds != 0);
	
	/* If we have a back-edge u-->v, 
	 * then we know that there is some path of tree edges
	 * v --> ... --> u. 
	 * We want to print that path. */
	
	// doing a fresh BFS (specific to this back edge), so clear paths
	//paths.clear();
	
	// BFS from the target node, looking for the path *back* to the source.
	PathMap::mapped_type::iterator i_e = paths.find(my_source)->second.begin();

	// print out the whole cycle
	auto print_edge = [g](Edge e)
	{
		assert(e.p_ds != 0);
		
		shared_ptr<encap::die> from_die = 
			boost::dynamic_pointer_cast<encap::die>((*e.p_ds)[e.referencing_off]);
		shared_ptr<encap::die> from_projected_die = 
			boost::dynamic_pointer_cast<encap::die>(source(e, g)->shared_from_this());
		shared_ptr<encap::die> to_die = 
			boost::dynamic_pointer_cast<encap::die>((*e.p_ds)[e.off]);
		shared_ptr<encap::die> to_projected_die = 
			boost::dynamic_pointer_cast<encap::die>(target(e, g)->shared_from_this());
		
		std::cerr << "@0x" << std::hex << e.referencing_off 
						<< " " << from_die->get_spec().tag_lookup(from_die->get_tag())
						<< " " << (from_die->has_attr(DW_AT_name) ? 
							   from_die->get_attr(DW_AT_name).get_string() : "(anonymous)") 
				  << ", projection "
					<< "@0x" << std::hex << from_projected_die->get_offset()
						<< " " << from_projected_die->get_spec().tag_lookup(from_projected_die->get_tag())
						<< " " << (from_projected_die->has_attr(DW_AT_name) ? 
							   from_projected_die->get_attr(DW_AT_name).get_string() : "(anonymous)") 
				<< endl
				  << " ---> @0x" << std::hex << e.off 
						<< " " << to_die->get_spec().tag_lookup(to_die->get_tag())
						<< " " << (to_die->has_attr(DW_AT_name) ? 
							   to_die->get_attr(DW_AT_name).get_string() : "(anonymous)") 
				  << ", projection "
					<< "@0x" << std::hex << to_projected_die->get_offset()
						<< " " << to_projected_die->get_spec().tag_lookup(to_projected_die->get_tag())
						<< " " << (to_projected_die->has_attr(DW_AT_name) ? 
							   to_projected_die->get_attr(DW_AT_name).get_string() : "(anonymous)") 
				<< std::endl;
	};
	
	cerr << "*** begin cycle" << endl;
	Edge cur_edge = e;
	Edge next_edge;
	for (PathMap::mapped_type::iterator i_e = paths.find(my_source)->second.begin(); 
			i_e != paths.find(my_source)->second.end();
			i_e++)
	{
		print_edge(*i_e);
	}
	print_edge(e);
	cerr << "*** end cycle" << endl;
}
template <class Edge, class Graph>
void cycle_handler::back_edge(Edge e, Graph& g)
{
	// Print it
	// print_back_edge_and_cycle(e, g);
	
	// Try to break it
	encap::basic_die *my_source = source(e, g);
	encap::basic_die *my_target = target(e, g);
	// Get the path back from target to source, i.e. the tree-edge path
	auto paths = get_bfs_paths(my_target, g); // from target, to source
	// paths[u] is the shortest path from my_target to u
	auto paths_found = paths.find(my_source);
	if (paths_found != paths.end())
	{
		PathMap::mapped_type::iterator i_e = paths_found->second.begin();

		auto remove_edge_if_fwddeclable
		 = [g, &new_forward_decls, &new_skipped_edges]
		 (Edge candidate_e)
		{
			auto e_source_projected = source(candidate_e, g);
			auto e_source_ultimate = dynamic_pointer_cast<encap::basic_die>(
				(*candidate_e.p_ds)[candidate_e.referencing_off]).get();
			auto e_target_projected = target(candidate_e, g);
			auto e_target_ultimate = dynamic_pointer_cast<encap::basic_die>(
				(*candidate_e.p_ds)[candidate_e.off]).get();
				
			// is the target of this edge forward-declarable?
			if ((e_target_projected->get_tag() == DW_TAG_structure_type
				&& e_target_projected->get_name())
				|| (e_target_ultimate->get_tag() == DW_TAG_structure_type
				&& e_target_ultimate->get_name())
				)
			{
				bool use_projected_target = 
					(e_target_projected->get_tag() == DW_TAG_structure_type
						&& e_target_projected->get_name());
				//if (std::find(g.forward_decls.begin(), g.forward_decls.end(),
				//	e_target) != g.forward_decls.end())
				//{
				//	std::cerr << "Cycle already broken in previous round by removing this edge; can continue."
				//		<< std::endl;
				//	return true;
				//}
				/*else*/ if (std::find(new_forward_decls.begin(), new_forward_decls.end(),
					e_target_projected) != new_forward_decls.end())
				{
					std::cerr << "Cycle already broken in this round by removing this edge; can continue."
						<< std::endl;
					return true;
				}			
				else
				{
					std::cerr << "Breaking cycle by skipping this edge." << std::endl;
					// remove the edge
					new_skipped_edges.push_back(candidate_e);

					// add the target to the forward-declare list
					new_forward_decls.push_back(
						use_projected_target ? e_target_projected : e_target_ultimate);

					// we can now exit the loop
					return true;
				}
			}
			return false;
		};

		bool removed = false;
		for (PathMap::mapped_type::iterator i_e = paths_found->second.begin();
				i_e != paths_found->second.end();
				++i_e)
		{
			if (remove_edge_if_fwddeclable(*i_e))
			{ removed = true; break; }
		}
		// if we didn't exit the loop early, we're not finished yet
		if (!removed)
		{
			removed = remove_edge_if_fwddeclable(e);
		}
		if (!removed)
		{
			cerr << "Could not break cycle as follows." << endl;
			print_back_edge_and_cycle(e, g, paths);

			assert(false);
		}
	}
	else
	{
		cerr << "No path from back_edge target to source!" << endl;
		assert(false);
	}
}

cpp_dependency_order::cpp_dependency_order(dwarf::encap::basic_die& parent)
	: /*is_initialized(false),*/ p_parent(&parent)//, cycle_hnd(paths, *this)
{
	unsigned old_total_edges_skipped = 0;
	unsigned new_total_edges_skipped = -1;
	unsigned cycle_count = 0;

	unsigned initial_graph_edge_count = 0;
	auto vs = boost::vertices(parent);
	for (boost::graph_traits<encap::basic_die>::vertex_iterator i_v =
			vs.first; i_v != vs.second; i_v++)
	{
		auto es = boost::out_edges(*i_v, parent);
		initial_graph_edge_count += srk31::count(es.first, es.second);
	}
	
	while (old_total_edges_skipped != new_total_edges_skipped)
	{
		old_total_edges_skipped = new_total_edges_skipped; 
		//cycle_handler::PathMap paths;
		std::vector<dwarf::encap::basic_die *> new_forward_decls;
		std::vector<dwarf::encap::attribute_value::weak_ref> new_skipped_edges;
		cycle_handler cycle_hnd(/*paths,*/ new_forward_decls, new_skipped_edges);
		
		// DEBUG: print all edges
		unsigned pre_graph_edge_count = 0;
		auto vs = vertices(*this);
		for (auto i_v = vs.first; i_v != vs.second; i_v++)
		{
			auto es = out_edges(*i_v, *this);
			pre_graph_edge_count += srk31::count(es.first, es.second);
		}	
		/* Find cycles and remove edges. */
		std::map<
			boost::graph_traits<encap::basic_die>::vertex_descriptor, 
			boost::default_color_type
		> underlying_dfs_node_color_map;
		auto dfs_color_map = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
				underlying_dfs_node_color_map
			);
		auto visitor = boost::visitor(cycle_hnd).color_map(dfs_color_map);
		// go!
		boost::depth_first_search(*this, visitor);

		// add the skipped edges
		for (auto i_skipped_edge = new_skipped_edges.begin(); 
			i_skipped_edge != new_skipped_edges.end();
			i_skipped_edge++) skipped_edges.push_back(*i_skipped_edge);
		for (auto i_forward_decl = new_forward_decls.begin(); 
			i_forward_decl != new_forward_decls.end();
			i_forward_decl++) forward_decls.push_back(*i_forward_decl);

		// count edges in the de-cycled graph
		unsigned post_graph_edge_count = 0;
		auto wvs = vertices(*this);
		//std::cerr << "diagraph Blah { " << std::endl;
		for (auto i_v = wvs.first; i_v != wvs.second; i_v++)
		{
			auto es = out_edges(*i_v, *this);
			post_graph_edge_count += srk31::count(es.first, es.second);
		}
		//std::cerr << "}" << std::endl;


		new_total_edges_skipped = skipped_edges.size();
		cerr << "On cycle " << cycle_count << " we started with "
			<< pre_graph_edge_count << " edges "
			<< "and ended with "
			<< post_graph_edge_count << " edges "
			<< "having skipped another "
			<< new_skipped_edges.size() << " edges." << endl;
		assert(post_graph_edge_count + skipped_edges.size() == initial_graph_edge_count);
		
		++cycle_count;
	}
	
	/* Find cycles and remove edges. AGAIN. */
	cerr << "Reached a fixed point after skipping " << skipped_edges.size() << " edges." << endl;
	std::cerr << "About to print remaining cycles (hopefully none)." << std::endl;

	std::map<
		boost::graph_traits<encap::basic_die>::vertex_descriptor, 
		boost::default_color_type
	> underlying_dfs_node_color_map_again;
	auto dfs_color_map_again = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
			underlying_dfs_node_color_map_again
		);
	std::vector<dwarf::encap::basic_die *> new_forward_decls_again;
	std::vector<dwarf::encap::attribute_value::weak_ref> new_skipped_edges_again;
	noop_cycle_handler cycle_hnd_again(new_forward_decls_again, new_skipped_edges_again);
	auto visitor_again = boost::visitor(cycle_hnd_again).color_map(dfs_color_map_again);
	boost::depth_first_search(*this, visitor_again);
	
	// DEBUG: verify that we have no cycle
	std::map<
		boost::graph_traits<cpp_dependency_order>::vertex_descriptor, 
		boost::default_color_type
	> underlying_topsort_node_color_map;
	 auto topsort_color_map = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
		underlying_topsort_node_color_map
	);
	auto named_params = boost::color_map(topsort_color_map);
	boost::topological_sort(*this, std::back_inserter(topsorted_container), named_params);
}
} } // end namespace dwarf::tool
