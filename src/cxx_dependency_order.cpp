#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <cmath>
#include <cstdlib>
#include <boost/algorithm/string.hpp>

#include <indenting_ostream.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/cxx_model.hpp>
#include <dwarfpp/cxx_dependency_order.hpp>
#include <srk31/algorithm.hpp>

using namespace srk31;
using namespace dwarf;
using namespace dwarf::lib;
using std::vector;
using std::set;
using std::map;
using std::string;
using std::cerr;
using std::endl;
using std::hex;
using std::dec;
using std::ostringstream;
using std::istringstream;
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

// const vector<string> dwarfidl_cxx_target::default_compiler_argv = { "g++",
// 	"-fno-eliminate-unused-debug-types",
// 	"-fno-eliminate-unused-debug-symbols"
// };
void dwarfidl_cxx_target::emit_forward_decls(const set<encap::basic_die *>& fds)
{
	out << "// begin a group of forward decls" << endl;
	for (auto i = fds.begin(); i != fds.end(); i++)
	{
		assert(((*i)->get_tag() == DW_TAG_structure_type
			 || (*i)->get_tag() == DW_TAG_union_type
			 || (*i)->get_tag() == DW_TAG_enumeration_type)
			&& (*i)->get_name());
			
		out << "struct " << protect_ident(*(*i)->get_name()) 
			<< "; // forward decl" << std::endl;
	}
	out << "// end a group of forward decls" << std::endl;
}

// this is the base case
void dwarfidl_cxx_target::emit_all_decls(shared_ptr<spec::file_toplevel_die> p_d)
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
	
	map< vector<string>, shared_ptr<spec::basic_die> > toplevel_decls_emitted;
	Dwarf_Off o = 0UL;
	for (abstract_dieset::iterator cu = p_d->children_begin(); 
				cu != p_d->children_end(); ++cu)
	{ 
		cpp_dependency_order order(*dynamic_pointer_cast<encap::basic_die>(*cu)); 
		emit_forward_decls(order.forward_decls); 
		for (cpp_dependency_order::container::iterator i = order.topsorted_container.begin(); 
				i != order.topsorted_container.end(); 
				i++) 
		{ 
			o = (*i)->get_offset();
			//if (!spec::file_toplevel_die::is_visible()(p_d)) continue; 
			dispatch_to_model_emitter( 
				out,
				dynamic_pointer_cast<encap::basic_die>((*i)->shared_from_this())->iterator_here(),
				// this is our predicate
				[&toplevel_decls_emitted, this](shared_ptr<spec::basic_die> p_d)
				{
					/* We check whether we've been declared already */
					auto opt_ident_path = p_d->ident_path_from_cu();
					if (opt_ident_path && opt_ident_path->size() == 1)
					{
						auto found = toplevel_decls_emitted.find(*opt_ident_path);
						if (found != toplevel_decls_emitted.end())
						{
							/* This means we would be redecling if we emitted here. */
							auto current_is_type
							 = dynamic_pointer_cast<spec::type_die>(p_d);
							auto previous_is_type
							 = dynamic_pointer_cast<spec::type_die>(found->second);
							auto print_name_parts = [](const vector<string>& ident_path)
							{
								for (auto i_name_part = ident_path.begin();
									i_name_part != ident_path.end(); ++i_name_part)
								{
									if (i_name_part != ident_path.begin()) cerr << " :: ";
									cerr << *i_name_part;
								}
							};
							
							/* In the case of types, we output a warning. */
							if (current_is_type && previous_is_type)
							{
								if (!current_is_type->is_rep_compatible(previous_is_type)
								||  !previous_is_type->is_rep_compatible(current_is_type))
								{
									cerr << "Warning: saw rep-incompatible types with "
											"identical toplevel names: ";
									print_name_parts(*opt_ident_path);
									cerr << endl;
								}
							}
							// we should skip this
							cerr << "Skipping redeclaration of DIE ";
							//print_name_parts(*opt_ident_path);
							cerr << p_d->summary();
							cerr << " already emitted as " 
								<< *toplevel_decls_emitted[*opt_ident_path]
								<< endl;
							return false;
						}
						
						/* At this point, we are going to give the all clear to emit.
						 * But we want to remember this, so we can skip future redeclarations
						 * that might conflict. */
						
						/* Some declarations are harmless to emit, because they never 
						 * conflict (forward decls). We won't bother remembering these. */
						auto is_program_element
						 = dynamic_pointer_cast<spec::program_element_die>(p_d);
						bool is_harmless_fwddecl = is_program_element
							&& is_program_element->get_declaration()
							&& *is_program_element->get_declaration();
						
						// if we got here, we will go ahead with emitting; 
						// if it generates a name, remember this!
						// NOTE that dwarf info has been observed to contain things like
						// DW_TAG_const_type, type structure (see evcnt in librump.o)
						// where the const type and the structure have the same name.
						// We won't use the name on the const type, so we use the
						// cxx_type_can_have_name helper to rule those cases out.
						auto is_type = dynamic_pointer_cast<spec::type_die>(p_d);
						if (
							(!is_type || (is_type && this->cxx_type_can_have_name(is_type)))
						&&  !is_harmless_fwddecl
						)
						{
							toplevel_decls_emitted.insert(make_pair(*opt_ident_path, p_d));
						}
					} // end if already declared with this 
					
					// not a conflict-creating redeclaration, so go ahead
					return true;
				}
			); 
		} 
	}
}

} } // end namespace dwarf::tool

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
		// PathMap::mapped_type::iterator i_path = paths_found->second.begin();

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
			// and will a forward declaration break the dependency?
			if (
				(
					e_target_projected->get_tag() == DW_TAG_structure_type
				 && e_target_projected->get_name()
				)
			||	(
					e_target_ultimate->get_tag() == DW_TAG_structure_type
				 && e_target_ultimate->get_name()
				)
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
					new_forward_decls.insert(
						use_projected_target ? e_target_projected : e_target_ultimate);

					// we can now exit the loop
					return true;
				}
			}
			return false;
		};
		
		/* We can only usefully remove an edge
		 * if it represents a dependency from a pointer type
		 * to a structure type. But this needn't be an immediate edge
		 * from a DW_TAG_pointer_type to a DW_TAG_structure_type --
		 * it might have intervening chained modifier DIEs like const, volatile etc.
		 * It might also have intervening typedefs.
		 * When we see a pointer type, we track subsequent chained types.
		 * If we see a structure type */

		bool removed = false;
		bool been_round_once_already = false;
		encap::pointer_type_die *coming_from_pointer;
		for (PathMap::mapped_type::iterator i_e = paths_found->second.begin();
				i_e != paths_found->second.end(); // will go round TWICE
				++i_e,
				been_round_once_already = 
					(!been_round_once_already && i_e == paths_found->second.end()) 
						? (i_e = paths_found->second.begin(), true)
						: been_round_once_already)
		{
			auto e_source_projected = source(*i_e, g);
			auto e_source_ultimate = dynamic_pointer_cast<encap::basic_die>(
				(*(*i_e).p_ds)[(*i_e).referencing_off]).get();
			auto e_target_projected = target(*i_e, g);
			auto e_target_ultimate = dynamic_pointer_cast<encap::basic_die>(
				(*(*i_e).p_ds)[(*i_e).off]).get();
			
			if (e_source_ultimate->get_tag() == DW_TAG_pointer_type
			 && i_e->referencing_attr == DW_AT_type)
			{
				coming_from_pointer = dynamic_cast<encap::pointer_type_die *>(e_source_ultimate);
				assert(coming_from_pointer);
			}
			else if (dynamic_cast<spec::type_chain_die *>(e_source_ultimate))
			{
				// it's another type chain, so do nothing
			}
			else coming_from_pointer = 0;
			// it's another DIE, not a pointer or type chain so reset it

			// we also require that one of the cycle's edges is the
			// type attr of a pointer type.
			// It doesn't have to be the one pointing to the struct --
			// we might have a pointer to a const struct. etc.. 
			if (coming_from_pointer && remove_edge_if_fwddeclable(*i_e))
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
	bool do_topsort = !getenv("DWARFIDL_NO_TOPSORT");
	if (do_topsort)
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
			old_total_edges_skipped = (new_total_edges_skipped == -1) ? 0 : new_total_edges_skipped; 
			//cycle_handler::PathMap paths;
			set<encap::basic_die *> new_forward_decls;
			vector<encap::attribute_value::weak_ref> new_skipped_edges;
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
			map<
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
				i_forward_decl++) forward_decls.insert(*i_forward_decl);

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

		map<
			graph_traits<encap::basic_die>::vertex_descriptor, 
			default_color_type
		> underlying_dfs_node_color_map_again;
		auto dfs_color_map_again = make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
				underlying_dfs_node_color_map_again
			);
		set<dwarf::encap::basic_die *> new_forward_decls_again;
		vector<dwarf::encap::attribute_value::weak_ref> new_skipped_edges_again;
		noop_cycle_handler cycle_hnd_again(new_forward_decls_again, new_skipped_edges_again);
		auto visitor_again = visitor(cycle_hnd_again).color_map(dfs_color_map_again);
		depth_first_search(*this, visitor_again);

		// DEBUG: verify that we have no cycle
		map<
			graph_traits<cpp_dependency_order>::vertex_descriptor, 
			default_color_type
		> underlying_topsort_node_color_map;
		 auto topsort_color_map = make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
			underlying_topsort_node_color_map
		);
		auto named_params = color_map(topsort_color_map);
		topological_sort(*this, std::back_inserter(topsorted_container), named_params);
	} // end if do_topsort
	else
	{
		/* Forward-decl everything we reasonably can, then
		 * add to skipped_edges every edge from a struct that is a
		 *   member->(type_chain->)*pointer_type
		 *   and the pointer_type is forward-decl'able.
		 * proceed in dieset order. */
		/* FIXME: we have also to remove the following classes of cycle:
		
		 1. structure member is a pointer to a subroutine type
		      subroutine type has a parameter that is a pointer
		         parameter points to the structure type
		    Q. how to break this cycle? 
		    A. We don't have to! 
		    struct foo {
				void (*memb)(struct foo *);
			};
			In other words, within a struct, cycles that go back to the same struct
			even via anonymous external DIEs (here the pointer type)
			are okay, 
			
		HMM. This code is horrible.
		Consider directly coding up a "must declare before" relation for toplevel DIEs.
		Why only toplevel? Hmm -- I think forward declarations only affect toplevel,
		or namespace-level in the case of C++. CHECK this though.
		
		 */
			
		auto is_fwd_declable = [](shared_ptr<spec::basic_die> p_d) {
			return p_d->get_name() && (
				p_d->get_tag() == DW_TAG_structure_type
			 || p_d->get_tag() == DW_TAG_union_type
			 || p_d->get_tag() == DW_TAG_enumeration_type);
			 // FIXME: more for C++ (but lots of other stuff needs fixing...)
		};
		
		for (auto i_die = p_parent->children_begin(); i_die != p_parent->children_end(); ++i_die)
		{
			if (is_fwd_declable(*i_die))
			{
				forward_decls.insert(dynamic_cast<encap::basic_die *>(i_die->get()));
			}
			//topsorted_container.push_back(dynamic_cast<encap::basic_die *>(i_die->get()));
			
		}
		vector<encap::attribute_value::weak_ref> new_skipped_edges;
		auto my_vertices = vertices(*this);
		for (auto i_vert = my_vertices.first; i_vert != my_vertices.second; ++i_vert)
		{
			auto my_edges = out_edges(*i_vert, *this);
			for (auto i_edge = my_edges.first; i_edge != my_edges.second; ++i_edge)
			{
				auto e_source_ultimate = dynamic_pointer_cast<encap::basic_die>(
					(*(*i_edge).p_ds)[(*i_edge).referencing_off]).get();
				auto e_target_ultimate = dynamic_pointer_cast<encap::basic_die>(
					(*(*i_edge).p_ds)[(*i_edge).off]).get();

				if (e_source_ultimate->get_tag() == DW_TAG_member
				 && (*i_edge).referencing_attr == DW_AT_type)
				{
					auto member = dynamic_cast<spec::member_die *>(e_source_ultimate);
					assert(member);
					shared_ptr<spec::type_die> member_type = member->get_type();
					auto member_type_chain = dynamic_pointer_cast<spec::type_chain_die>(member_type);
					//   ^ this one may be null
					while (member_type
					   && (
					   	member_type->get_tag() == DW_TAG_array_type
						|| (
					       member_type_chain
					   &&  member_type->get_tag() != DW_TAG_typedef)))
					{
						if (member_type->get_tag() == DW_TAG_array_type)
						{
							member_type = dynamic_pointer_cast<spec::array_type_die>(member_type)
								->get_type();
						}
						else member_type = member_type_chain->get_type();
						member_type_chain = dynamic_pointer_cast<spec::type_chain_die>(member_type);
					}
					// Here we "end" at the named type mentioned by the member's type
					// (e.g. "char" if it's a char *[]).
					
					// If we end at something forward-decl'able, 
					// then we will be forward-declaring it, so
					// we can skip this edge.
					if (member_type && 
						is_fwd_declable(dynamic_pointer_cast<spec::basic_die>(member_type)))
					{
						new_skipped_edges.push_back(*i_edge);
					}
				}
				if (e_source_ultimate->get_tag() == DW_TAG_formal_parameter
				 && (*i_edge).referencing_attr == DW_AT_type)
				{
					auto fp = dynamic_cast<spec::formal_parameter_die *>(e_source_ultimate);
					assert(fp);
					shared_ptr<spec::type_die> fp_type = fp->get_type();
					auto fp_type_chain = dynamic_pointer_cast<spec::type_chain_die>(fp_type);
					//   ^ this one may be null
					while (fp_type
					   && (
					   	fp_type->get_tag() == DW_TAG_array_type
						|| (
					       fp_type_chain
					   &&  fp_type->get_tag() != DW_TAG_typedef)))
					{
						if (fp_type->get_tag() == DW_TAG_array_type)
						{
							fp_type = dynamic_pointer_cast<spec::array_type_die>(fp_type)
								->get_type();
						}
						else fp_type = fp_type_chain->get_type();
						fp_type_chain = dynamic_pointer_cast<spec::type_chain_die>(fp_type);
					}
					// Here we "end" at the named type mentioned by the fp's type
					// (e.g. "char" if it's a char *[]).
					
					// If we end at something forward-decl'able, 
					// then we will be forward-declaring it, so
					if (fp_type && 
						is_fwd_declable(dynamic_pointer_cast<spec::basic_die>(fp_type)))
					{
						new_skipped_edges.push_back(*i_edge);
					}
				}
			}
		}
		for (auto i_skipped_edge = new_skipped_edges.begin(); 
			i_skipped_edge != new_skipped_edges.end();
			i_skipped_edge++) skipped_edges.push_back(*i_skipped_edge);
		
	
		// Now we hope we have something we hope is acyclic -- check that
		map<
			graph_traits<encap::basic_die>::vertex_descriptor, 
			default_color_type
		> underlying_dfs_node_color_map_again;
		auto dfs_color_map_again = make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
				underlying_dfs_node_color_map_again
			);
		set<dwarf::encap::basic_die *> new_forward_decls_again;
		vector<dwarf::encap::attribute_value::weak_ref> new_skipped_edges_again;
		noop_cycle_handler cycle_hnd_again(new_forward_decls_again, new_skipped_edges_again);
		auto visitor_again = visitor(cycle_hnd_again).color_map(dfs_color_map_again);
		depth_first_search(*this, visitor_again);

		map<
			graph_traits<cpp_dependency_order>::vertex_descriptor, 
			default_color_type
		> underlying_topsort_node_color_map;
		 auto topsort_color_map = make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
			underlying_topsort_node_color_map
		);
		auto named_params = color_map(topsort_color_map);
		topological_sort(*this, std::back_inserter(topsorted_container), named_params);
	}
}

} } // end namespace dwarf::tool
