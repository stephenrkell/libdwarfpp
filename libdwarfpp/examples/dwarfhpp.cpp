#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <sstream>

#include <indenting_ostream.hpp>
#include <dwarfpp/abstract.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/cxx_compiler.hpp>
#include "dwarfhpp.hpp"

using namespace srk31;
using namespace dwarf;
using namespace dwarf::lib;

namespace dwarf { namespace tool {
typedef dwarf::abstract::Die_abstract_base<dwarf::encap::die> adie;

static std::string name_for_subprogram(dwarf::tool::cxx_compiler& compiler,
	adie& d);
static std::string name_for_type(dwarf::tool::cxx_compiler& compiler,
	dwarf::encap::Die_encap_is_type& d, boost::optional<const std::string&> infix_typedef_name = boost::optional<const std::string&>());
static std::string name_for_argument(dwarf::tool::cxx_compiler& compiler,
	adie& d, int argnum);
static void emit_typedef(std::ostream& out, dwarf::tool::cxx_compiler& compiler, 
	const std::string& name, encap::Die_encap_is_type& d);
static std::string create_ident_for_anonymous_die(adie& d);
static std::string create_ident_for_anonymous_die(adie& d)
{
	assert(!d.get_name());
    std::ostringstream s;
    s << "_dwarfhpp_anon_" << std::hex << d.get_offset();
    return s.str();
}

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
	std::cerr << "Found a back-edge! Declaration of DIEe at 0x" << std::hex << e.referencing_off << std::dec;
    encap::Die_encap_base *my_source = source(e, g);
        //&dynamic_cast<dwarf::encap::Die_encap_base&>(*(*(e.p_ds))[e.referencing_off]);
    encap::Die_encap_base *my_target = target(e, g);
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
    	dwarf::encap::Die_encap_base *, 
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
}}
namespace dwarf { namespace tool {
void emit_forward_decls(std::vector<dwarf::encap::Die_encap_base *> fds)
{
	std::cout << "// begin a group of forward decls" << std::endl;
    for (auto i = fds.begin(); i != fds.end(); i++)
    {
    	assert((*i)->get_tag() == DW_TAG_structure_type
        	&& (*i)->get_name());
        std::cout << "struct " << *(*i)->get_name() << "; // forward decl" << std::endl;
    }
	std::cout << "// end a group of forward decls" << std::endl;
}

// until I have g++ 4.5 for_each + lambda...
#define recurse_on_children(d) \
	{ out.inc_level(); \
	for (dwarf::encap::Die_encap_base::children_iterator i = d.children_begin(); \
    		i != d.children_end(); \
            i++) \
    { \
    	auto new_context = context; \
        new_context.push_back(&d); \
        emit_decls<0>(out, compiler, \
        	dynamic_cast<abstract::tag<encap::die, 0>::type&>(**i), new_context); \
    } \
	out.dec_level(); }
#define recurse_on_toplevel_children(d) \
	for (dwarf::encap::Die_encap_base::children_iterator cu = d.children_begin(); \
    		cu != d.children_end(); \
            cu++) \
    { \
        cpp_dependency_order order(**cu); \
        emit_forward_decls(order.forward_decls); \
	    for (cpp_dependency_order::container::iterator i = order.topsorted_container.begin(); \
    		    i != order.topsorted_container.end(); \
                i++) \
        { \
    	    if (!dwarf::encap::Die_encap_all_compile_units::is_visible()(dynamic_cast<dwarf::encap::Die_encap_base&>( \
        	    **i))) continue; \
    	    auto new_context = context; \
            new_context.push_back(&d); \
            emit_decls<0>(out, compiler, \
        	    dynamic_cast<abstract::tag<encap::die, 0>::type&>(**i), new_context); \
        } \
    }
#define recurse_on_topsorted_children(d) \
	{ out.inc_level(); \
    cpp_dependency_order order(d); \
    emit_forward_decls(order.forward_decls); \
	for (auto i = order.container.begin(); \
    		i != order.container.end(); \
            i++) \
    { \
    	auto new_context = context; \
        dwarf::encap::Die_encap_base& encap = dynamic_cast<dwarf::encap::Die_encap_base&>( \
        	**i); \
        new_context.push_back(&d); \
        emit_decls<0>(out, compiler, \
        	dynamic_cast<abstract::tag<encap::die, 0>::type&>(encap), new_context); \
    } \
	out.dec_level(); }

// our emitter is a function template
template <Dwarf_Half Tag> 
void emit_decls(
	indenting_ostream& out, dwarf::tool::cxx_compiler& compiler,
	typename abstract::tag<encap::die, Tag>::type& d, 
	std::vector<dwarf::encap::Die_encap_base *>& context);
// default does nothing except warn us
template <Dwarf_Half Tag> 
void emit_decls(
	indenting_ostream& out, dwarf::tool::cxx_compiler& compiler,
	typename abstract::tag<encap::die, Tag>::type& d, 
	std::vector<dwarf::encap::Die_encap_base *>& context)
{
	std::cerr << "Warning: hit unsupported tag: " << d.get_tag() << std::endl;
}
// declare specializations here
template <> 
void emit_decls<0>(indenting_ostream& out, dwarf::tool::cxx_compiler& compiler,
	abstract::tag<encap::die, 0>::type& d, 
	std::vector<dwarf::encap::Die_encap_base *>& context);
#define proto_for_specialization(fragment) \
template<> \
void emit_decls<DW_TAG_ ## fragment>(indenting_ostream& out, dwarf::tool::cxx_compiler& compiler, \
	abstract::tag<encap::die, DW_TAG_ ## fragment>::type& d, \
    std::vector<dwarf::encap::Die_encap_base*>& context)

proto_for_specialization(base_type);
proto_for_specialization(subprogram);
proto_for_specialization(formal_parameter);
proto_for_specialization(unspecified_parameters);

// what others do we need in order to handle C code?
proto_for_specialization(array_type);
proto_for_specialization(enumeration_type);
proto_for_specialization(member);
proto_for_specialization(pointer_type);
proto_for_specialization(structure_type);
proto_for_specialization(subroutine_type);
proto_for_specialization(typedef);
proto_for_specialization(union_type);
proto_for_specialization(const_type);
proto_for_specialization(constant);
proto_for_specialization(enumerator);
proto_for_specialization(variable);
proto_for_specialization(volatile_type);
proto_for_specialization(restrict_type);
proto_for_specialization(subrange_type);

// define specializations here
proto_for_specialization(base_type)
{
    boost::optional<std::string> type_name_in_compiler = 
        compiler.name_for(boost::dynamic_pointer_cast<spec::base_type_die>(d.get_this()));
    if (!type_name_in_compiler) return; // FIXME: could define a C++ ADT!

    std::string our_name_for_this_type = name_for_type(compiler,
    	dynamic_cast<encap::Die_encap_is_type&>(d));

    if (our_name_for_this_type != *type_name_in_compiler)
    {
        out << "typedef " << *type_name_in_compiler 
        	<< ' ' << our_name_for_this_type
            << ';' << std::endl;
    }
}        
proto_for_specialization(subprogram)
{
    // skip unnamed, for now (FIXME)
    if (!d.get_name() || (*d.get_name()).empty()) return;

    // wrap with extern "lang"
    switch(dynamic_cast<dwarf::encap::Die_encap_compile_unit&>(
        d.get_parent()).get_language())
    {
        case DW_LANG_C:
        case DW_LANG_C89:
        case DW_LANG_C99:
        	if (d.get_calling_convention() && *d.get_calling_convention() != DW_CC_normal)
            {
            	std::cerr << "Warning: skipping subprogram with nonstandard calling convention: "
                	<< d << std::endl;
                return;
            }
            out << "extern \"C\" { ";
            break;
        default:
            assert(false);
    }

    out 	<< (d.get_type() 
        	? name_for_type(compiler, dynamic_cast<dwarf::encap::Die_encap_is_type&>(**d.get_type())) 
            : std::string("void"))
        << ' '
        << *d.get_name()
        << '(';        

	// recurse on children
    recurse_on_children(d);
	// end the prototype
    out	<< ");";
    // close the extern block
    out << "}" << std::endl;
}
proto_for_specialization(formal_parameter)
{
    // recover arg position
    int argpos = 0;
    dwarf::encap::Die_encap_subprogram *p_subp = 
        dynamic_cast<dwarf::encap::Die_encap_subprogram*>(context.back());
    dwarf::encap::formal_parameters_iterator i = p_subp->formal_parameters_begin();
    while (i != p_subp->formal_parameters_end() && (*i)->get_offset() != d.get_offset()) { argpos++; i++; }
    assert(i != p_subp->formal_parameters_end());

    if (argpos != 0) out << ", ";

    out	<< (d.get_type() ? name_for_type(compiler, dynamic_cast<encap::Die_encap_is_type&>(**d.get_type())) : "int")
        << ' '
        << name_for_argument(compiler, d, argpos);
}
proto_for_specialization(unspecified_parameters)
{
    // were there any specified args?
    dwarf::encap::Die_encap_subprogram *p_subp = 
        dynamic_cast<dwarf::encap::Die_encap_subprogram*>(context.back());

    if (p_subp->formal_parameters_begin() != p_subp->formal_parameters_end()) out << ", ";
}
proto_for_specialization(array_type) 
{
	// use typedef again, and infix the name
    out << "typedef ";
    if (d.get_name()) out << name_for_type(compiler, d, *d.get_name());
    else out << name_for_type(compiler, d, create_ident_for_anonymous_die(d));
    out << ";" << std::endl;
}
proto_for_specialization(enumeration_type) 
{
	out << "enum " << (d.get_name() ? *d.get_name() : create_ident_for_anonymous_die(d))
    	<< " { " << std::endl;
    recurse_on_children(d);
    out << std::endl << "};" << std::endl;
}
proto_for_specialization(member) 
{
	/* To reproduce the member's alignment, we always issue an align attribute. 
     * We choose our alignment so as to ensure that the emitted field is located
     * at the offset specified in DWARF. */
	encap::Die_encap_is_type& member_type = 
    	dynamic_cast<encap::Die_encap_is_type&>(**d.get_type());

	// recover the previous formal parameter's offset and size
    dwarf::encap::Die_encap_has_named_children *p_type = 
        dynamic_cast<dwarf::encap::Die_encap_has_named_children*>(context.back());
    assert(p_type);
    dwarf::encap::named_children_iterator i = p_type->named_children_begin();
    dwarf::encap::named_children_iterator prev_i = p_type->named_children_begin();
    // HACK: "initialize" prev_i to end (if we do this straight up, it breaks the copy constructor)
    while (prev_i != p_type->named_children_end()) prev_i++;
    
    while (i != p_type->named_children_end() && ((*i)->get_tag() != DW_TAG_member || 
    	dynamic_cast<encap::Die_encap_member*>(*i)->get_offset() != d.get_offset())) 
        { prev_i = i; i++; }
    assert(i != p_type->named_children_end()); // would mean we failed to find ourselves
    Dwarf_Unsigned cur_offset;
    if (prev_i == p_type->named_children_end()) cur_offset = 0;
    else 
    {
    	encap::Die_encap_member& prev_member = dynamic_cast<encap::Die_encap_member&>(**prev_i);

		//std::cerr << "Previous member actual type: " << dynamic_cast<encap::die&>((*prev_member.get_type())) << std::endl;
        //std::cerr << "Previous member concrete type: " << dynamic_cast<encap::die&>((*prev_member.get_type()).get_concrete_type()) << std::endl;
    	assert(prev_member.get_type());

	    if (prev_member.get_data_member_location())
        {
        	auto prev_member_calculated_byte_size 
             = (*prev_member.get_type())->calculate_byte_size();
            if (!prev_member_calculated_byte_size)
            {
            	std::cerr << "couldn't calculate size of data member " << prev_member
                	<< std::endl;
                assert(false);
            }
        	cur_offset = dwarf::lib::evaluator(
        		prev_member.get_data_member_location()->at(0)/*.m_expr*/, d.get_ds().get_spec(),
            	// push zero as the initial stack value
            	std::stack<Dwarf_Unsigned>(std::deque<Dwarf_Unsigned>(1, 0UL))).tos()
            	//+ encap::Die_encap_is_type::calculate_byte_size(**prev_member.get_type());
            	+ *prev_member_calculated_byte_size;
        }
        else
        {
        	std::cerr << "no data member location: context die is " << *context.back() << std::endl;
        	cur_offset = (context.back()->get_tag() == DW_TAG_union_type) 
            	? 0 : (assert(false), 0);
        }
    }
    //std::cerr << "Calculated next available offset is " << cur_offset << std::endl;
     
	if (d.get_name()) out << name_for_type(compiler, member_type, *d.get_name());
    else out << name_for_type(compiler, member_type, boost::optional<const std::string&>());
    out	<< " ";
    if (!compiler.type_infixes_name(member_type.get_this())) out << *d.get_name();
    
    if (d.get_data_member_location() && (*d.get_data_member_location()).size() == 1)
    {
    	Dwarf_Unsigned offset = dwarf::lib::evaluator(
        	d.get_data_member_location()->at(0)/*.m_expr*/, d.get_ds().get_spec(),
            // push zero as the initial stack value
            std::stack<Dwarf_Unsigned>(std::deque<Dwarf_Unsigned>(1, 0UL))).tos();
            
		/* Calculate a sensible align value for this. We could just use the offset,
         * but that might upset the compiler if it's larger than what it considers
         * the reasonable biggest alignment for the architecture. So pick a factor
         * of the alignment s.t. no other factors exist between cur_off and offset.*/
	    //std::cerr << "Aligning member to offset " << offset << std::endl;
        
        Dwarf_Unsigned candidate_factor, next_factor = offset;
        do
        {
        	candidate_factor = next_factor;
            
            //std::cerr << "Looking for a factor strictly greater than " << (offset - cur_offset)
            	// << " and smaller than " << candidate_factor << std::endl;
            
        	// a better factor would be smaller...            
            while (--next_factor > 1 && offset % next_factor != 0);
            
            //std::cerr << "Considering factor " << next_factor << std::endl;
            
            // ... but not too small        
        } while (next_factor > (offset - cur_offset) && next_factor < candidate_factor);
        
        //std::cerr << "Settled on factor " << candidate_factor << std::endl;
        
        if (candidate_factor != 0) out << " __attribute__((aligned(" << candidate_factor << ")))";
	    out << ";" << " // offset: " << offset << std::endl;
    }
    else 
    {
    	// guess at word alignment
    	out << " __attribute__((aligned(sizeof(int))))"; 
    	out << "; // no DW_AT_data_member_location, so it's a guess" << std::endl;
	}
    
    //out << std::endl;
}
proto_for_specialization(pointer_type)
{
    // we always emit a typedef with synthetic name
    // (but the user could just use the pointed-to type and "*")

   // std::cerr << "pointer type: " << d << std::endl;
	//assert(!d.get_name() || (d.get_type() && (*d.get_type()).get_tag() == DW_TAG_subroutine_type));
    
    //out << "typedef ";
    // we expect a pointed-to type, but might be void
    std::string name_to_use = d.get_name() ? compiler.cxx_name_from_string(*d.get_name(), "_dwarfhpp_") : 
    	create_ident_for_anonymous_die(d);
    if (!d.get_type()) out << "typedef void *" << name_to_use << ";" << std::endl;
    else emit_typedef(out, compiler, name_to_use, d);
//     out << name_for_type(compiler, d, d.get_name());
//     	//<< (d.get_type() ? compiler.name_for_type(*d.get_type()) : "void")
//         //<< " * "
//     if (d.get_name())
//     {
//     	out << *d.get_name();
//     }
//     else
//     {
//     	out << create_ident_for_anonymous_die(d);
//     }
//         
//     out << ";" << std::endl;
}
proto_for_specialization(structure_type) 
{
	out << "struct " << (d.get_name() ? *d.get_name() : create_ident_for_anonymous_die(d))
    	<< " { " << std::endl;
    recurse_on_children(d);
    out << "} __attribute__((packed));" << std::endl;
}
proto_for_specialization(subroutine_type) 
{
	//out << name_for_type(compiler, dynamic_cast<encap::Die_encap_is_type&>(*d.get_type()),
    //	d.get_name() ? *d.get_name() : boost::optional<const std::string&>())
     //   << ";" << std::endl;
    std::cerr << "Warning: assuming subroutine type at 0x" << std::hex << d.get_offset() 
    	<< std::dec << " is the target of some pointer type; skipping." << std::endl;
}
void emit_typedef(std::ostream& out, 
	dwarf::tool::cxx_compiler& compiler, const std::string& name, encap::Die_encap_is_type& d)
{
    std::string name_to_use = compiler.cxx_name_from_string(name, "_dwarfhpp_");
            
    out << "typedef " 
    	<< name_for_type(compiler, d,
        	name_to_use);
    // HACK: we use the infix for subroutine types
    if (!compiler.type_infixes_name(d.get_this()))
    {
        out << " "
	    	<< name_to_use;
    }
    out << ";" << std::endl;
}
proto_for_specialization(typedef)
{
	assert(d.get_name());
    if (!d.get_type())
    {
    	std::cerr << "Warning: assuming `int' for typeless typedef: " << d << std::endl;
        out << "typedef int " << *d.get_name() << ";" << std::endl;
        return;
    }
    emit_typedef(out, compiler, *d.get_name(), dynamic_cast<encap::Die_encap_is_type&>(**d.get_type()));
}
proto_for_specialization(union_type) 
{
	out << "union " << (d.get_name() ? *d.get_name() : create_ident_for_anonymous_die(d))
    	<< " { " << std::endl;
    recurse_on_children(d);
    out << "};" << std::endl;
}
proto_for_specialization(const_type) 
{
    // we always emit a typedef with synthetic name
    // (but the user could just use the pointed-to type and "const")
    out << "typedef " << name_for_type(compiler, d) << " ";
    out << create_ident_for_anonymous_die(d)
        << ";" << std::endl;

}
proto_for_specialization(constant) {}
proto_for_specialization(enumerator) 
{
	// FIXME
    if ((*dynamic_cast<encap::Die_encap_enumeration_type&>(*context.back())
    	.enumerators_begin())
        	->get_offset() != d.get_offset()) // .. then we're not the first, so
            out << ", " << std::endl;
    out << *d.get_name();
}
proto_for_specialization(variable) {}
proto_for_specialization(volatile_type) {}
proto_for_specialization(restrict_type) {}
proto_for_specialization(subrange_type) 
{
	// Since we can't express subranges directly in C++, we just
    // emit a typedef of the underlying type.
    out << "typedef " << name_for_type(compiler, dynamic_cast<encap::Die_encap_is_type&>(*d.get_type()))
    	<< " " << (d.get_name() ? *d.get_name() : create_ident_for_anonymous_die(d))
        << ";" << std::endl;
}

// this is the zero case
template <> 
void emit_decls<0>(
	indenting_ostream& out, dwarf::tool::cxx_compiler& compiler,
	abstract::tag<encap::die, 0>::type& d, 
	std::vector<dwarf::encap::Die_encap_base *>& context)
{
#define CASE(fragment) \
	case DW_TAG_ ## fragment:	\
    { void (*func)(srk31::indenting_ostream&, dwarf::tool::cxx_compiler&, \
    	dwarf::abstract::tag<encap::die, DW_TAG_ ## fragment>::type &, \
        std::vector<dwarf::encap::Die_encap_base *>&) \
        = emit_decls<DW_TAG_ ## fragment> ; assert(func); } \
	emit_decls<DW_TAG_ ## fragment>(out, compiler, \
    	dynamic_cast<dwarf::abstract::tag<encap::die, DW_TAG_ ## fragment>::type &>(d), context); break;

    //if (context.size() > 0 && context.back() == &d) 
    //{
    //	std::cerr << "Unhandled child! Tag: "
    //    	<< d.get_ds().get_spec().tag_lookup(d.get_tag())
    //        << std::endl;
    //    assert(false);
    //}
    
    // if it's a compiler builtin, skip it
    if (compiler.is_builtin(d.get_this())) return;
    
	switch(d.get_tag())
    {
    	case 0: {
			// got an all_compile_units; recurse on children
            recurse_on_toplevel_children((dynamic_cast<encap::Die_encap_all_compile_units&>(d)));
            } break;
        case DW_TAG_compile_unit: // we use all_compile_units so this shouldn't happen
        	assert(false);
        	break;
		CASE(subprogram)
        CASE(base_type)
        CASE(typedef)
        CASE(structure_type)
        CASE(pointer_type)
        CASE(volatile_type)
        CASE(formal_parameter)
        CASE(array_type)
        CASE(enumeration_type)
        CASE(member)
        CASE(subroutine_type)
        CASE(union_type)
        CASE(const_type)
        CASE(constant)
        CASE(enumerator)
        CASE(variable)
        CASE(restrict_type)
        CASE(subrange_type)   
        CASE(unspecified_parameters)
        // The following tags we silently pass over without warning
        case DW_TAG_condition:
        case DW_TAG_lexical_block:
        case DW_TAG_label:
        	break;
        default:
        	std::cerr 	<< "Warning: ignoring tag " 
            			<< d.get_ds().get_spec().tag_lookup(d.get_tag())
                        << std::endl;
            break;
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
    dwarf::encap::Die_encap_all_compile_units all_cus = def.ds().all_compile_units();
    dwarf::abstract::tag<encap::die, 0>::type & all_cus_as_base = all_cus;
    
    /* Build a repertoire of DWARF base types out of our compiler-native
     * builtin types. */
    std::vector<std::string> args;
    args.push_back("g++");
    args.push_back("-fno-eliminate-unused-debug-types");
    args.push_back("-fno-eliminate-unused-debug-symbols");
    dwarf::tool::cxx_compiler compiler(args);
    
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
    

	/* In general, we want to emit typedefs, structures, function protos etc.
     * with the same names as found in the DWARF info. BUT what if they're
     * C++ reserved words? We do two things:
     *  - emit a warning
     *  - use the name _dwarfhpp_(name)_ 
     */
    indenting_ostream& s = srk31::indenting_cout;
    
    std::vector<dwarf::encap::Die_encap_base *> initial_context_vec;
	dwarf::tool::emit_decls<0>(s, compiler, all_cus_as_base, initial_context_vec);

    // for each
    return 0;
}
namespace dwarf { namespace tool {

// generic version -- tries to dispatch to a specialisation
//template <Dwarf_Half Tag>
//void emit_decls(indenting_ostream& out, dwarf::tool::cxx_compiler compiler,
//	Type& d, 
//    std::vector<dwarf::encap::Die_encap_base*>& context)

static std::string name_for_type(dwarf::tool::cxx_compiler& compiler,
	encap::Die_encap_is_type& d, boost::optional<const std::string&> infix_typedef_name)
{
	return compiler.cxx_declarator_from_type_die(
    	boost::dynamic_pointer_cast<spec::type_die>(d.get_this()), 
        infix_typedef_name);
}    
static std::string name_for_subprogram(dwarf::tool::cxx_compiler& compiler,
	adie& d)
{
    std::string our_name_for_this_subprogram = 
        compiler.is_reserved(*d.get_name()) ? 
                ("_dwarfhpp_" + *d.get_name())
          : ( compiler.is_valid_cxx_ident(*d.get_name()) ? *d.get_name()
              	: ("_dwarfhpp_" + compiler.make_valid_cxx_ident(*d.get_name())));

	return our_name_for_this_subprogram;
}    
static std::string name_for_argument(dwarf::tool::cxx_compiler& compiler,
	adie& d, int argnum)
{
	std::ostringstream s;
    //std::cerr << "called name_for_argument on argument position " << argnum << ", ";
    if (d.get_name()) { /*std::cerr << "named ";*/ s << "_dwarfhpp_arg_" << *d.get_name(); /*std::cerr << *d.get_name();*/ }
    else { /*std::cerr << "anonymous";*/ s << "_dwarfhpp_anon_arg_" << argnum; }
    //std::cerr << std::endl;
    return s.str();
} 
cpp_dependency_order::cpp_dependency_order(dwarf::encap::Die_encap_base& parent)
    : /*is_initialized(false),*/ p_parent(&parent)//, cycle_det(paths, *this)
{
	unsigned old_skipped_edges_count = 0;
    unsigned new_skipped_edges_count = -1;
    while (old_skipped_edges_count != new_skipped_edges_count)
    {
    	old_skipped_edges_count = new_skipped_edges_count; 
	    cycle_detector::PathMap paths;
	    std::vector<dwarf::encap::Die_encap_base *> new_forward_decls;
        std::vector<dwarf::encap::attribute_value::weak_ref> new_skipped_edges;
	 	cycle_detector cycle_det(paths, new_forward_decls, new_skipped_edges);
        
	    // DEBUG: print all edges
        unsigned total_edge_count = 0;
        auto vs = boost::vertices(parent);
        for (boost::graph_traits<encap::Die_encap_base>::vertex_iterator i_v =
    		    vs.first; i_v != vs.second; i_v++)
        {
    	    auto es = boost::out_edges(*i_v, parent);
    	    for (boost::graph_traits<encap::Die_encap_base>::out_edge_iterator i_e =
        	    es.first; i_e != es.second; i_e++)
            {
        	    //std::cerr << "Node at " << std::hex << boost::source(*i_e, parent)->get_offset()
                //	<< " depends on node at " << std::hex << boost::target(*i_e, parent)->get_offset()
                //    << " because of edge from offset " << i_e->referencing_off
                //    << " to offset " << i_e->off
                //    << " with attribute " 
                //    << parent.get_ds().get_spec().attr_lookup(i_e->referencing_attr)
                //    << std::endl;
                total_edge_count++;
            }
	    }    
	    /* Find cycles and remove edges. */
        std::map<
    	    boost::graph_traits<encap::Die_encap_base>::vertex_descriptor, 
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
                //    << " because of edge from offset " << i_e->referencing_off
                //    << " to offset " << i_e->off
                //    << " with attribute " 
                //    << parent.get_ds().get_spec().attr_lookup(i_e->referencing_attr)
                //    << std::endl;
                //std::cerr << std::hex << source(*i_e, *this)->get_offset() << " --> " 
            	//    << std::hex << target(*i_e, *this)->get_offset() << ";" << std::endl;
                weeded_edge_count++;
            }
	    }
        std::cerr << "}" << std::endl;
	    new_skipped_edges_count = skipped_edges.size();
        assert(weeded_edge_count + new_skipped_edges_count == total_edge_count);

// 	    /* Find cycles and remove edges. AGAIN. */
//         std::cerr << "About to check for cycles AGAIN." << std::endl;
//         std::map<
//     	    boost::graph_traits<encap::Die_encap_base>::vertex_descriptor, 
//     	    boost::default_color_type
//         > underlying_dfs_node_color_map_again;
//         auto dfs_color_map_again = boost::make_assoc_property_map( // ColorMap provides a mutable "Color" property per node
//         	    underlying_dfs_node_color_map_again
//             );
//         cycle_detector::PathMap paths_again;
//         cycle_detector cycle_det_again(paths, *this);
//         auto visitor_again = boost::visitor(cycle_det_again).color_map(dfs_color_map_again);
//         boost::depth_first_search(parent, visitor_again);
//         //is_initialized = true;

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
