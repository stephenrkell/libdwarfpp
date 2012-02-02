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
template <class Edge, class Graph>
void cycle_detector::back_edge(Edge e, Graph& g)
{
	std::cerr << "Found a back-edge! Declaration of DIEe at 0x" 
		<< std::hex << e.referencing_off << std::dec;
	encap::basic_die *my_source = source(e, g);
		//&dynamic_cast<dwarf::encap::Die_encap_base&>(*(*(e.p_ds))[e.referencing_off]);
	encap::basic_die *my_target = target(e, g);
		//&dynamic_cast<dwarf::encap::Die_encap_base&>(*(*(e.p_ds))[e.off]);
	 std::cerr << ", name " <<
				( my_source->has_attr(DW_AT_name) ?
				  my_source->get_attr(DW_AT_name).get_string() : "(anonymous)") 
			<< " depends on declaration of DIE at 0x" << std::hex << e.off << std::dec << ", name " <<
				( my_target->has_attr(DW_AT_name) ?
				  my_target->get_attr(DW_AT_name).get_string() : "(anonymous)") 
			<< " owing to attribute " << e.p_ds->get_spec().attr_lookup(e.referencing_attr)
			<< " from DIE at offset " << std::hex << e.referencing_off << ", name "
			<< (boost::dynamic_pointer_cast<encap::die>((*e.p_ds)[e.referencing_off])->has_attr(DW_AT_name) ? 
				boost::dynamic_pointer_cast<encap::die>((*e.p_ds)[e.referencing_off])->get_attr(DW_AT_name).get_string() : "(anonymous)") 
			<< " to DIE at offset " << std::hex << e.off << ", name "
			<< (boost::dynamic_pointer_cast<encap::die>((*e.p_ds)[e.off])->has_attr(DW_AT_name) ? 
				boost::dynamic_pointer_cast<encap::die>((*e.p_ds)[e.off])->get_attr(DW_AT_name).get_string() : "(anonymous)") 
			<< std::endl;
	assert(e.p_ds != 0);
	// BFS from the target node...
	std::map<
		dwarf::encap::basic_die *, 
		typename boost::default_color_type
	> underlying_bfs_node_color_map;
	auto bfs_color_map = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
		underlying_bfs_node_color_map
	); 
	bfs_path_recorder<PathMap> vis(paths);
	auto visitor = boost::visitor(vis).color_map(bfs_color_map);
	boost::breadth_first_search(g, target(e, g), visitor);
	/// now we have the tree-path and a back-edge that completes the cycle
	assert(paths.find(my_source) != paths.end()); // hmm, true for a self-loop?
	PathMap::mapped_type::iterator i_e = paths.find(my_source)->second.begin();
	for (; 
			i_e != paths.find(my_source)->second.end();
			i_e++)
	{
		auto e = *i_e;
		assert(e.p_ds != 0);
		
		std::cerr << "Following edge from DIE at offset " << std::hex << e.referencing_off << ", name "
			<< (boost::dynamic_pointer_cast<encap::die>((*e.p_ds)[e.referencing_off])->has_attr(DW_AT_name) ? 
				boost::dynamic_pointer_cast<encap::die>((*e.p_ds)[e.referencing_off])->get_attr(DW_AT_name).get_string() : "(anonymous)") 
			<< " to DIE at offset " << std::hex << e.off << ", name "
			<< (boost::dynamic_pointer_cast<encap::die>((*e.p_ds)[e.off])->has_attr(DW_AT_name) ? 
				boost::dynamic_pointer_cast<encap::die>((*e.p_ds)[e.off])->get_attr(DW_AT_name).get_string() : "(anonymous)") 
			<< std::endl;
			
			
			// << *source(e, g)
			//<< " to " << *target(e, g) << std::endl;

		// is the target of this edge forward-declarable?
		if (target(e, g)->get_tag() == DW_TAG_structure_type
			&& target(e, g)->get_name())
		{
			if (std::find(g.forward_decls.begin(), g.forward_decls.end(),
				target(e, g)) != g.forward_decls.end())
			{
				std::cerr << "Cycle already broken in previous round by removing this edge; can continue."
					<< std::endl;
				break;
			}
			else if (std::find(new_forward_decls.begin(), new_forward_decls.end(),
				target(e, g)) != new_forward_decls.end())
			{
				std::cerr << "Cycle already broken in this round by removing this edge; can continue."
					<< std::endl;
				break;
			}			
			else
			{
				std::cerr << "Breaking cycle by skipping this edge." << std::endl;
				// remove the edge
				new_skipped_edges.push_back(*i_e);

				// add the target to the forward-declare list
				new_forward_decls.push_back(target(e, g));

				// we can now exit the loop
				break;
			}
		}
	}
	// if we didn't exit the loop early, we're not finished yet
	if (i_e == paths.find(my_source)->second.end())
	{
		// last gasp: target of back-edge is a struct
		std::cerr << "Following edge from " << *my_source
			<< " to " << *my_target << std::endl;
		if (my_target->get_tag() == DW_TAG_structure_type)
		{
			if (std::find(g.forward_decls.begin(), g.forward_decls.end(),
				my_target) != g.forward_decls.end())
			{
				std::cerr << "Cycle already broken in previous round by removing this edge; can continue."
					<< std::endl;
			}
			else if (std::find(new_forward_decls.begin(), new_forward_decls.end(),
				my_target) != new_forward_decls.end())
			{
				std::cerr << "Cycle already broken in this round by removing this edge; can continue."
					<< std::endl;
			}
			else
			{
				std::cerr << "Breaking cycle by skipping this edge." << std::endl;
				// remove the edge
				new_skipped_edges.push_back(e);

				// add the target to the forward-declare list
				new_forward_decls.push_back(my_target);
			}
		}
		else
		{
			assert(false);
		}
	}
}

cpp_dependency_order::cpp_dependency_order(dwarf::encap::basic_die& parent)
	: /*is_initialized(false),*/ p_parent(&parent)//, cycle_det(paths, *this)
{
	unsigned old_skipped_edges_count = 0;
	unsigned new_skipped_edges_count = -1;
	while (old_skipped_edges_count != new_skipped_edges_count)
	{
		old_skipped_edges_count = new_skipped_edges_count; 
		cycle_detector::PathMap paths;
		std::vector<dwarf::encap::basic_die *> new_forward_decls;
		std::vector<dwarf::encap::attribute_value::weak_ref> new_skipped_edges;
	 	cycle_detector cycle_det(paths, new_forward_decls, new_skipped_edges);
		
		// DEBUG: print all edges
		unsigned total_edge_count = 0;
		auto vs = boost::vertices(parent);
		for (boost::graph_traits<encap::basic_die>::vertex_iterator i_v =
				vs.first; i_v != vs.second; i_v++)
		{
			auto es = boost::out_edges(*i_v, parent);
			for (boost::graph_traits<encap::basic_die>::out_edge_iterator i_e =
				es.first; i_e != es.second; i_e++)
			{
				//std::cerr << "Node at " << std::hex << boost::source(*i_e, parent)->get_offset()
				//	<< " depends on node at " << std::hex << boost::target(*i_e, parent)->get_offset()
				//	<< " because of edge from offset " << i_e->referencing_off
				//	<< " to offset " << i_e->off
				//	<< " with attribute " 
				//	<< parent.get_ds().get_spec().attr_lookup(i_e->referencing_attr)
				//	<< std::endl;
				total_edge_count++;
			}
		}	
		/* Find cycles and remove edges. */
		std::map<
			boost::graph_traits<encap::basic_die>::vertex_descriptor, 
			boost::default_color_type
		> underlying_dfs_node_color_map;
	   auto dfs_color_map = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
				underlying_dfs_node_color_map
			);
		auto visitor = boost::visitor(cycle_det).color_map(dfs_color_map);
		boost::depth_first_search(*this, visitor);
		//is_initialized = true;

		// count edges in the de-cycled graph
		unsigned weeded_edge_count = 0;
		auto wvs = vertices(*this);
  		std::cerr << "diagraph Blah { " << std::endl;
		for (boost::graph_traits<cpp_dependency_order>::vertex_iterator i_v =
			wvs.first; i_v != wvs.second; i_v++)
		{

			auto es = out_edges(*i_v, *this);
			for (boost::graph_traits<cpp_dependency_order>::out_edge_iterator i_e =
				es.first; i_e != es.second; i_e++)
			{
				//std::cerr << "Node at " << std::hex << boost::source(*i_e, parent)->get_offset()
				//	<< " depends on node at " << std::hex << boost::target(*i_e, parent)->get_offset()
				//	<< " because of edge from offset " << i_e->referencing_off
				//	<< " to offset " << i_e->off
				//	<< " with attribute " 
				//	<< parent.get_ds().get_spec().attr_lookup(i_e->referencing_attr)
				//	<< std::endl;
				//std::cerr << std::hex << source(*i_e, *this)->get_offset() << " --> " 
				//	<< std::hex << target(*i_e, *this)->get_offset() << ";" << std::endl;
				weeded_edge_count++;
			}
		}
		std::cerr << "}" << std::endl;
		new_skipped_edges_count = skipped_edges.size();
		assert(weeded_edge_count + new_skipped_edges_count == total_edge_count);

// 		/* Find cycles and remove edges. AGAIN. */
//		 std::cerr << "About to check for cycles AGAIN." << std::endl;
//		 std::map<
//	 		boost::graph_traits<encap::Die_encap_base>::vertex_descriptor, 
//	 		boost::default_color_type
//		 > underlying_dfs_node_color_map_again;
//		 auto dfs_color_map_again = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
//		 		underlying_dfs_node_color_map_again
//			 );
//		 cycle_detector::PathMap paths_again;
//		 cycle_detector cycle_det_again(paths, *this);
//		 auto visitor_again = boost::visitor(cycle_det_again).color_map(dfs_color_map_again);
//		 boost::depth_first_search(parent, visitor_again);
//		 //is_initialized = true;

		for (auto i_skipped_edge = new_skipped_edges.begin(); 
			i_skipped_edge != new_skipped_edges.end();
			i_skipped_edge++) skipped_edges.push_back(*i_skipped_edge);
		for (auto i_forward_decl = new_forward_decls.begin(); 
			i_forward_decl != new_forward_decls.end();
			i_forward_decl++) forward_decls.push_back(*i_forward_decl);
	}
	
	// DEBUG:verify that we have no cycle
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
