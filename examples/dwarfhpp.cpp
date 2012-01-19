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
	
	void 
	emit_typedef(
		shared_ptr<spec::type_die> p_d,
		const string& name
	)
	{ out << make_typedef(p_d, name); }

	// our emitter is a function template
	template <typename T> 
	void emit_decls(
		T& d, 
		const std::vector<dwarf::encap::basic_die *>& context);
	
	void 
	emit_forward_decls(vector<encap::basic_die *> fds);
};

// declare specializations here
#define proto_for_specialization(fragment) \
template<> \
void dwarfhpp_cxx_target::emit_decls<encap:: fragment ## _die>( \
	encap:: fragment ## _die & d, \
	const vector<encap::basic_die*>& context)

#define PROTO(fragment) proto_for_specialization(fragment)
PROTO(base_type);
PROTO(subprogram);
PROTO(formal_parameter);
PROTO(unspecified_parameters);
PROTO(array_type);
PROTO(enumeration_type);
PROTO(member);
PROTO(pointer_type);
PROTO(structure_type);
PROTO(subroutine_type);
PROTO(typedef);
PROTO(union_type);
PROTO(const_type);
PROTO(constant);
PROTO(enumerator);
PROTO(variable);
PROTO(volatile_type);
PROTO(restrict_type);
PROTO(subrange_type);
#undef PROTO

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

// forward declarations of the templates we use for dependency order
// template <typename Value>
// skip_edge_iterator<Value>::skip_edge_iterator(
// 	Base p, Base begin, Base end, const cpp_dependency_order& deps);
// template <typename Value>
// bool skip_edge_iterator<Value>::is_skipped(Base b);
// template <typename Value>
// void skip_edge_iterator<Value>::increment();
// template <typename Value>
// void skip_edge_iterator<Value>::decrement();
// template <class Edge, class Graph>
// void cycle_detector::back_edge(Edge e, Graph& g);

// until I have g++ 4.5 for_each + lambda...
#define recurse_on_children(d) \
	{ out.inc_level(); \
	for (dwarf::spec::abstract_dieset::iterator i = d.children_begin(); \
			i != d.children_end(); \
			i++) \
	{ \
		auto new_context = context; \
		new_context.push_back(&d); \
		emit_decls( \
			*dynamic_pointer_cast<encap::basic_die>(*i), new_context); \
	} \
	out.dec_level(); }
#define recurse_on_toplevel_children(d) \
	for (dwarf::spec::abstract_dieset::iterator cu = d.children_begin(); \
			cu != d.children_end(); \
			cu++) \
	{ \
		cpp_dependency_order order(*dynamic_pointer_cast<encap::basic_die>(*cu)); \
		emit_forward_decls(order.forward_decls); \
		for (cpp_dependency_order::container::iterator i = order.topsorted_container.begin(); \
				i != order.topsorted_container.end(); \
				i++) \
		{ \
			if (!dwarf::spec::file_toplevel_die::is_visible()((*i)->shared_from_this())) continue; \
			auto new_context = context; \
			new_context.push_back(&d); \
			emit_decls( \
				*dynamic_pointer_cast<encap::basic_die>((*i)->shared_from_this()), new_context); \
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
		dwarf::encap::basic_die& encap = *dynamic_pointer_cast<encap::basic_die>( \
			*i); \
		new_context.push_back(&d); \
		emit_decls( \
			encap, new_context); \
	} \
	out.dec_level(); }

#define PROTO(fragment) proto_for_specialization(fragment)

// define specializations here
PROTO(base_type)
{
	optional<string> type_name_in_compiler = 
		name_for(dynamic_pointer_cast<spec::base_type_die>(d.get_this()));
		
	if (!type_name_in_compiler) return; // FIXME: could define a C++ ADT!

	string our_name_for_this_type = name_for_type(
		dynamic_pointer_cast<spec::type_die>(d.shared_from_this()), 
		0 /* no infix */, 
		false /* no friendly names*/);

	if (our_name_for_this_type != *type_name_in_compiler)
	{
		out << "typedef " << *type_name_in_compiler
			<< ' ' << protect_ident(our_name_for_this_type)
			<< ';' << endl;
	}
}

PROTO(subprogram)
{
	// skip unnamed, for now (FIXME)
	if (!d.get_name() || (*d.get_name()).empty()) return;

	// wrap with extern "lang"
	switch(dynamic_pointer_cast<encap::compile_unit_die>(
		d.enclosing_compile_unit())->get_language())
	{
		case DW_LANG_C:
		case DW_LANG_C89:
		case DW_LANG_C99:
			if (d.get_calling_convention() && *d.get_calling_convention() != DW_CC_normal)
			{
				cerr << "Warning: skipping subprogram with nonstandard calling convention: "
					<< d << endl;
				return;
			}
			out << "extern \"C\" { ";
			break;
		default:
			assert(false);
	}

	out  << (d.get_type() 
				? protect_ident(name_for_type(dynamic_pointer_cast<spec::type_die>(d.get_type()))) 
				: std::string("void")
			)
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

PROTO(formal_parameter)
{
	// recover arg position
	int argpos = 0;
	encap::subprogram_die *p_subp = 
		dynamic_cast<encap::subprogram_die *>(context.back());

	auto i = p_subp->formal_parameter_children_begin();
	while (i != p_subp->formal_parameter_children_end() 
		&& (*i)->get_offset() != d.get_offset()) 
	{ argpos++; i++; }
	
	assert(i != p_subp->formal_parameter_children_end());

	if (argpos != 0) out << ", ";

	out	<< (d.get_type() 
			? protect_ident(name_for_type(dynamic_pointer_cast<spec::type_die>(d.get_type())))
			: m_untyped_argument_typename
			)
		<< ' '
		<< protect_ident(
			name_for_argument(
				dynamic_pointer_cast<spec::formal_parameter_die>(
					d.shared_from_this()
				), argpos
			)
		);
}
PROTO(unspecified_parameters)
{
	// were there any specified args?
	encap::subprogram_die *p_subp = 
		dynamic_cast<encap::subprogram_die *>(context.back());

	if (p_subp->formal_parameter_children_begin()
		 != p_subp->formal_parameter_children_end())
	{
		out << ", ";
	}
}

PROTO(array_type) 
{
	emit_typedef(dynamic_pointer_cast<spec::type_die>(d.shared_from_this()),
		create_ident_for_anonymous_die(d.shared_from_this()));
}
PROTO(enumeration_type) 
{
	out << "enum " 
		<< protect_ident(
			(d.get_name() 
				? *d.get_name() 
				: create_ident_for_anonymous_die(d.shared_from_this())
			)
		)
		<< " { " << std::endl;
		
	recurse_on_children(d);
	
	out << endl << "};" << endl;
}
PROTO(member) 
{
	/* To reproduce the member's alignment, we always issue an align attribute. 
	 * We choose our alignment so as to ensure that the emitted field is located
	 * at the offset specified in DWARF. */
	shared_ptr<spec::type_die> member_type = 
		dynamic_pointer_cast<spec::type_die>(d.get_type());

	// recover the previous formal parameter's offset and size
	shared_ptr<spec::with_data_members_die> p_type = 
		dynamic_pointer_cast<spec::with_data_members_die>(
			context.back()->shared_from_this());
	assert(p_type);
	
	auto i = p_type->member_children_begin();
	auto prev_i = p_type->member_children_end();
	
	while (i != p_type->member_children_end() 
		&& (*i)->get_offset() != d.get_offset()) 
	{ prev_i = i; i++; }
	// now i points to us, and prev_i to our predecessor
	assert(i != p_type->member_children_end()); // would mean we failed to find ourselves
	
	Dwarf_Unsigned cur_offset;
	if (prev_i == p_type->member_children_end()) cur_offset = 0;
	else 
	{
		encap::member_die& prev_member = dynamic_cast<encap::member_die&>(**prev_i);

		//std::cerr << "Previous member actual type: " << dynamic_cast<encap::die&>((prev_member.get_type())) << std::endl;
		//std::cerr << "Previous member concrete type: " << dynamic_cast<encap::die&>((prev_member.get_type()).get_concrete_type()) << std::endl;
		assert(prev_member.get_type());

		if (prev_member.get_data_member_location())
		{
			auto prev_member_calculated_byte_size 
			 = prev_member.get_type()->calculate_byte_size();
			if (!prev_member_calculated_byte_size)
			{
				cerr << "couldn't calculate size of data member " << prev_member
					<< endl;
				assert(false);
			}
			cur_offset = evaluator(
				prev_member.get_data_member_location()->at(0), 
				d.get_ds().get_spec(),
				stack<Dwarf_Unsigned>(
					// push zero as the initial stack value
					deque<Dwarf_Unsigned>(1, 0UL)
					)
				).tos()
				+ *prev_member_calculated_byte_size;
		}
		else
		{
			cerr << "no data member location: context die is " << *context.back() << endl;
			cur_offset = (context.back()->get_tag() == DW_TAG_union_type) 
				? 0 : (assert(false), 0);
		}
	}
	 
	if (d.get_name())
	{
		out << protect_ident(
				name_for_type(member_type, *d.get_name())
				);
	}
	else 
	{
		out << protect_ident(
				name_for_type(member_type, optional<const string&>())
			);
	}
	out	<< " ";
	if (!type_infixes_name(member_type->get_this()))
	{
		out << protect_ident(*d.get_name());
	}
	
	if (d.get_data_member_location() && (*d.get_data_member_location()).size() == 1)
	{
		Dwarf_Unsigned offset = evaluator(
			d.get_data_member_location()->at(0), 
			d.get_ds().get_spec(),
			stack<Dwarf_Unsigned>(
				// push zero as the initial stack value
				deque<Dwarf_Unsigned>(1, 0UL)
				)
			).tos();
			
		if (offset > 0)
		{
			/* Calculate a sensible align value for this. We could just use the offset,
			 * but that might upset the compiler if it's larger than what it considers
			 * the reasonable biggest alignment for the architecture. So pick a factor
			 * of the alignment s.t. no other multiples exist between cur_off and offset. */
			//std::cerr << "Aligning member to offset " << offset << std::endl;

			// just search upwards through powers of two...
			// until we find one s.t.
			// searching upwards from cur_offset to factors of this power of two,
			// our target offset is the first one we find
			unsigned power = 1;
			bool is_good = false;
			do
			{
				unsigned test_offset = cur_offset;
				while (test_offset % power != 0) test_offset++;
				// now test_offset is a multiple of power -- check it's our desired offset
				is_good = (test_offset == offset);
			} while (!is_good && (power <<= 1, true));

			out << " __attribute__((aligned(" << power << ")))";
		}
		out << ";" 
			/*<< " static_assert(offsetof(" 
			<< name_for_type(compiler, p_type, boost::optional<const std::string&>())
			<< ", "
			<< protect_ident(*d.get_name())
			<< ") == " << offset << ");" */<< " // offset: " << offset << endl;
	}
	else 
	{
		// guess at word alignment
		out << " __attribute__((aligned(sizeof(int))))"; 
		out << "; // no DW_AT_data_member_location, so it's a guess" << std::endl;
	}
}

PROTO(pointer_type)
{
	// we always emit a typedef with synthetic name
	// (but the user could just use the pointed-to type and "*")

	// std::cerr << "pointer type: " << d << std::endl;
	//assert(!d.get_name() || (d.get_type() && d.get_type()->get_tag() == DW_TAG_subroutine_type));
	
	std::string name_to_use
	 = d.get_name() 
	 ? cxx_name_from_string(*d.get_name(), "_dwarfhpp_") 
	 : create_ident_for_anonymous_die(d.shared_from_this());
	 
	if (!d.get_type())
	{
		out << "typedef void *" 
			<< protect_ident(name_to_use) 
			<< ";" << std::endl;
	}
	else 
	{
		emit_typedef(dynamic_pointer_cast<spec::type_die>(d.shared_from_this()),
			name_to_use);
	}
}
PROTO(structure_type) 
{
	out << "struct " 
		<< protect_ident(
			d.get_name() 
			?  *d.get_name() 
			: create_ident_for_anonymous_die(d.shared_from_this())
			)
		<< " { " << std::endl;
		
	recurse_on_children(d);
	
	out << "} __attribute__((packed));" << std::endl;
}
PROTO(subroutine_type) 
{
	//out << name_for_type(compiler, dynamic_cast<encap::Die_encap_is_type&>(d.get_type()),
	//	d.get_name() ? *d.get_name() : boost::optional<const std::string&>())
	 //   << ";" << std::endl;
	std::cerr << "Warning: assuming subroutine type at 0x" << std::hex << d.get_offset() 
		<< std::dec << " is the target of some pointer type; skipping." << std::endl;
}

PROTO(typedef)
{
	assert(d.get_name());
	if (!d.get_type())
	{
		//std::cerr << "Warning: assuming `int' for typeless typedef: " << d << std::endl;
		std::cerr << "Warning: using `void' for typeless typedef: " << d << std::endl;		
		out << "typedef void " << protect_ident(*d.get_name()) << ";" << std::endl;
		return;
	}
	emit_typedef(
		dynamic_pointer_cast<spec::type_die>(d.get_type()),
		*d.get_name() 
	);
}
PROTO(union_type) 
{
	out << "union " 
		<< protect_ident(
			d.get_name() 
			? *d.get_name() 
			: create_ident_for_anonymous_die(d.shared_from_this())
			)
		<< " { " << std::endl;
		
	recurse_on_children(d);
	
	out << "};" << std::endl;
}

PROTO(const_type) 
{
	// we always emit a typedef with synthetic name
	// (but the user could just use the pointed-to type and "const")

	try
	{
		emit_typedef(
			dynamic_pointer_cast<spec::type_die>(d.shared_from_this()),
			create_ident_for_anonymous_die(d.shared_from_this())
		);
	}
	catch (dwarf::lib::Not_supported)
	{
		/* This happens when the debug info contains (broken) 
		 * qualified types that can't be expressed in a single declarator,
		 * e.g. (const (array t)). 
		 * Work around it by getting the name we would have used for a typedef
		 * of the anonymous DIE defining the typedef'd-to (target) type. 
		 * FIXME: bug in cases where we didn't output such
		 * a typedef! */
		
		out << "typedef " << create_ident_for_anonymous_die(d.get_type())
			<< " const " << create_ident_for_anonymous_die(d.shared_from_this()) 
			<< ";" << endl;
	}
}
PROTO(volatile_type) 
{
	// we always emit a typedef with synthetic name
	// (but the user could just use the pointed-to type and "const")

	try
	{
		emit_typedef(
			dynamic_pointer_cast<spec::type_die>(d.shared_from_this()),
			create_ident_for_anonymous_die(d.shared_from_this())
		);
	}
	catch (dwarf::lib::Not_supported)
	{
		/* See note for const_type above. */
		
		out << "typedef " << create_ident_for_anonymous_die(d.get_type())
			<< " volatile " << create_ident_for_anonymous_die(d.shared_from_this()) 
			<< ";" << endl;
	}
}
PROTO(constant) {}
PROTO(enumerator) 
{
	// FIXME
	if ((*dynamic_cast<encap::enumeration_type_die&>(*context.back())
		.enumerator_children_begin())
			->get_offset() != d.get_offset()) // .. then we're not the first, so
			out << ", " << endl;
	out << protect_ident(*d.get_name());
}
PROTO(variable) {}
PROTO(restrict_type) {}
PROTO(subrange_type) 
{
	// Since we can't express subranges directly in C++, we just
	// emit a typedef of the underlying type.
	out << "typedef " 
		<< protect_ident(name_for_type( 
			dynamic_pointer_cast<spec::type_chain_die>(d.shared_from_this())->get_type())
			)
		<< " " 
		<< protect_ident(
			d.get_name() 
			? *d.get_name() 
			: create_ident_for_anonymous_die(d.shared_from_this())
			)
		<< ";" << std::endl;
}

#undef PROTO

// this is the zero case
template <typename T> 
void dwarfhpp_cxx_target::emit_decls(
	T& d, 
	const std::vector<dwarf::encap::basic_die *>& context)
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
	 
#define CASE(fragment) \
	case DW_TAG_ ## fragment:	\
	dwarfhpp_cxx_target::emit_decls( \
		dynamic_cast<encap:: fragment ## _die &>(d), context); break;

	//if (context.size() > 0 && context.back() == &d) 
	//{
	//	std::cerr << "Unhandled child! Tag: "
	//		<< d.get_ds().get_spec().tag_lookup(d.get_tag())
	//		<< std::endl;
	//	assert(false);
	//}
	
	// if it's a compiler builtin, skip it
	if (is_builtin(d.get_this())) return;
	
	switch(d.get_tag())
	{
		case 0: {
			// got an all_compile_units; recurse on children
			recurse_on_toplevel_children(dynamic_cast<encap::file_toplevel_die&>(d));
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
			cerr 	<< "Warning: ignoring tag " 
						<< d.get_ds().get_spec().tag_lookup(d.get_tag())
						<< endl;
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
	shared_ptr<dwarf::encap::file_toplevel_die> all_cus = def.ds().all_compile_units();
	auto all_cus_as_base = all_cus;

	indenting_ostream& s = srk31::indenting_cout;
	dwarf::tool::dwarfhpp_cxx_target target(" ::cake::unspecified_wordsize_type", s);
	
	target.emit_decls(*all_cus, std::vector<encap::basic_die *>());

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
