/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * cxx_model.cpp: tools for constructing binary-compatible C++ models 
 * 			of DWARF elements.
 *
 * Copyright (c) 2009--2012, Stephen Kell.
 */

#include "cxx_model.hpp"
#include <boost/algorithm/string.hpp>

using std::vector;
using std::map;
using std::string;
using std::cerr;
using std::endl;
using std::hex;
using std::dec;
using std::ostringstream;
using boost::optional;
using boost::shared_ptr;
using boost::dynamic_pointer_cast;
using namespace dwarf::lib;

namespace dwarf {
namespace tool {

	string 
	cxx_generator::cxx_name_from_string(const string& s, const char *prefix)
	{
		if (is_reserved(s)) 
		{
			cerr << "Warning: generated C++ name `" << (prefix + s) 
				<< " from reserved word " << s << endl;
			return prefix + s; // being reserved implies s is lexically okay
		}
		else if (is_valid_cxx_ident(s)) return s;
		else // something is lexically illegal about s
		{
			return make_valid_cxx_ident(s);
		}
	}
	
	// static function
	bool 
	cxx_generator::is_reserved(const string& word)
	{
		return std::find(cxx_reserved_words.begin(),
			cxx_reserved_words.end(), 
			word) != cxx_reserved_words.end();
	}
	
	// static function
	bool 
	cxx_generator::is_valid_cxx_ident(const string& word)
	{
		static const boost::regex e("[a-zA-Z_][a-zA-Z0-9_]*");
		return !is_reserved(word) &&
			regex_match(word, e);	
	}
	
	string 
	cxx_generator::make_valid_cxx_ident(const string& word)
	{
		// FIXME: make this robust to illegal characters other than spaces
		string working = word;
		return is_valid_cxx_ident(word) ? word
			: (std::replace(working.begin(), working.end()-1, ' ', '_'), working);
	}
	
	string 
	cxx_generator::name_from_name_parts(const vector<string>& parts) 
	{
		ostringstream s;
		for (auto i_part = parts.begin(); i_part != parts.end(); i_part++)
		{
			if (i_part != parts.begin()) s << "::";
			s << *i_part;
		}
		return s.str();
	}
	
	bool 
	cxx_generator_from_dwarf::type_infixes_name(shared_ptr<spec::basic_die> p_d)
	{
		auto t = dynamic_pointer_cast<spec::type_die>(p_d);
		cerr << "Does this type infix a name? " << *t << endl;
		assert(t);
		auto unq_t = t->get_unqualified_type();
		return 
			unq_t && (
			unq_t->get_tag() == DW_TAG_subroutine_type
			||  unq_t->get_tag() == DW_TAG_array_type
			|| 
			(unq_t->get_tag() == DW_TAG_pointer_type &&
				dynamic_pointer_cast<spec::pointer_type_die>(unq_t)->get_type()
				&& 
				dynamic_pointer_cast<spec::pointer_type_die>(unq_t)->get_type()
					->get_tag()	== DW_TAG_subroutine_type)
				);
	}

	string 
	cxx_generator_from_dwarf::cxx_name_from_die(shared_ptr<spec::basic_die> p_d)
	{
		if (p_d->get_name()) return cxx_name_from_string(*p_d->get_name(), "_dwarfhpp_");
		else 
		{
			ostringstream s;
			s << "_dwarfhpp_anon_" << hex << p_d->get_offset();
			return s.str();
		}
	}
	
	bool cxx_generator_from_dwarf::cxx_type_can_be_qualified(shared_ptr<spec::type_die> p_d)
	{
		if (p_d->get_tag() == DW_TAG_array_type) return false;
		return true; // FIXME: correct?
	}

	string 
	cxx_generator_from_dwarf::cxx_declarator_from_type_die(
		shared_ptr<spec::type_die> p_d, 
		optional<const string&> infix_typedef_name/*= optional<const std::string&>()*/,
		bool use_friendly_names /*= true*/,  
		optional<const string&> extra_prefix /* = optional<const string&>() */,
		bool use_struct_and_union_prefixes /* = true */ )
	{
		string name_prefix;
		string qualifier_suffix;
		switch (p_d->get_tag())
		{
			// return the friendly compiler-determined name or not, depending on argument
			case DW_TAG_base_type:
				return 
				((extra_prefix && !use_friendly_names) ? *extra_prefix : "")
				+ local_name_for(dynamic_pointer_cast<spec::base_type_die>(p_d),
					use_friendly_names);
			case DW_TAG_typedef:
				return (extra_prefix ? *extra_prefix : "") + *p_d->get_name();
			case DW_TAG_pointer_type: {
				shared_ptr<spec::pointer_type_die> pointer 
				 = dynamic_pointer_cast<spec::pointer_type_die>(p_d);
				if (pointer->get_type())
				{
					if (pointer->get_type()->get_tag() == DW_TAG_subroutine_type)
					{
						// we have a pointer to a subroutine type -- pass on the infix name
						return cxx_declarator_from_type_die(
							pointer->get_type(), optional<const string&>(), 
							use_friendly_names, extra_prefix, 
							use_struct_and_union_prefixes);
					}
					else return cxx_declarator_from_type_die(
						pointer->get_type(), optional<const string&>(),
						use_friendly_names, extra_prefix, 
							use_struct_and_union_prefixes) + "*";
				}
				else return "void *";
			}
			case DW_TAG_array_type: {
				// we only understand C arrays, for now
				int language = dynamic_pointer_cast<spec::type_die>(p_d)
					->enclosing_compile_unit()->get_language();
				assert(language == DW_LANG_C89 
					|| language == DW_LANG_C 
					|| language == DW_LANG_C99);
				shared_ptr<spec::array_type_die> arr
				 = dynamic_pointer_cast<spec::array_type_die>(p_d);
				// calculate array size, if we have a subrange type
				auto array_size = arr->element_count();
				ostringstream arrsize; 
				if (array_size) arrsize << *array_size;
				return cxx_declarator_from_type_die(arr->get_type(), 
							optional<const string&>(), 
							use_friendly_names, extra_prefix, 
							use_struct_and_union_prefixes)
					+ " " + (infix_typedef_name ? *infix_typedef_name : "") + "[" 
					// add size, if we have a subrange type
					+ arrsize.str()
					+ "]";
			}
			case DW_TAG_subroutine_type: {
				ostringstream s;
				shared_ptr<spec::subroutine_type_die> subroutine_type 
				 = dynamic_pointer_cast<spec::subroutine_type_die>(p_d);
				s << (subroutine_type->get_type() 
					? cxx_declarator_from_type_die(subroutine_type->get_type(),
					optional<const string&>(), 
							use_friendly_names, extra_prefix, 
							use_struct_and_union_prefixes
					) 
					: string("void "));
				s << "(*" << (infix_typedef_name ? *infix_typedef_name : "")
					<< ")(";
				try
				{	
					for (auto i = p_d->get_first_child(); // terminated by exception
							i; i = i->get_next_sibling(), s << ", ")
					{
						switch (i->get_tag())
						{
							case DW_TAG_formal_parameter:
								s << cxx_declarator_from_type_die( 
										dynamic_pointer_cast<spec::formal_parameter_die>(i)->get_type(),
											optional<const string&>(), 
											use_friendly_names, extra_prefix, 
											use_struct_and_union_prefixes
										);
								break;
							case DW_TAG_unspecified_parameters:
								s << "...";
								break;
							default: assert(false); break;
						}
					}
				}
				catch (lib::No_entry) { s << ")"; }
				return s.str();
			}
			case DW_TAG_const_type:
				qualifier_suffix = " const";
				goto handle_qualified_type;
			case DW_TAG_volatile_type:
				qualifier_suffix = " volatile";
				goto handle_qualified_type;
			case DW_TAG_structure_type:
				if (use_struct_and_union_prefixes) name_prefix = "struct ";
				goto handle_named_type;
			case DW_TAG_union_type:
				if (use_struct_and_union_prefixes) name_prefix = "union ";
				goto handle_named_type;
			case DW_TAG_class_type:
				if (use_struct_and_union_prefixes) name_prefix = "class ";
				goto handle_named_type;
			handle_named_type:
			default:
				return (extra_prefix ? *extra_prefix : "") + name_prefix + cxx_name_from_die(p_d);
			handle_qualified_type: {
				/* This is complicated by the fact that array types in C/C++ can't be qualified directly,
				 * but such qualified types can be defined using typedefs. (FIXME: I think this is correct
				 * but don't quote me -- there might just be some syntax I'm missing.) */
				auto chained_type =  dynamic_pointer_cast<spec::type_chain_die>(p_d)->get_type();
				/* Note that many DWARF emitters record C/C++ "const void" (as in "const void *")
				 * as a const type with no "type" attribute. So handle this case. */
				if (!chained_type) return "void" + qualifier_suffix;
				else if (cxx_type_can_be_qualified(chained_type))
				{
					return cxx_declarator_from_type_die(
						chained_type, 
						infix_typedef_name,
						use_friendly_names, extra_prefix, 
						use_struct_and_union_prefixes
						) + qualifier_suffix;
				}
				else throw Not_supported(string("C++ qualifiers for types of tag ")
					 + p_d->get_spec().tag_lookup(chained_type->get_tag()));
			}
		}
	}
	
	bool 
	cxx_generator_from_dwarf::is_builtin(shared_ptr<spec::basic_die> p_d)
	{ 
		bool retval = p_d->get_name() && p_d->get_name()->find("__builtin_") == 0;
		if (retval) cerr << 
			"Warning: DIE at 0x" 
			<< hex << p_d->get_offset() << dec 
			<< " appears builtin and will be ignored." << endl;
		return retval; 
	}

	vector<string> 
	cxx_generator_from_dwarf::local_name_parts_for(
		shared_ptr<spec::basic_die> p_d,
		bool use_friendly_names /* = true */)
	{ 
		if (use_friendly_names && p_d->get_tag() == DW_TAG_base_type)
		{
			auto opt_name = name_for_base_type(dynamic_pointer_cast<spec::base_type_die>(p_d));
			if (opt_name) return vector<string>(1, *opt_name);
			else assert(false);
		}
		else
		{
			return vector<string>(1, cxx_name_from_die(p_d));
		}
	}

	vector<string> 
	cxx_generator_from_dwarf::fq_name_parts_for(shared_ptr<spec::basic_die> p_d)
	{
		if (p_d->get_offset() != 0UL && p_d->get_parent()->get_tag() != DW_TAG_compile_unit)
		{
			auto parts = fq_name_parts_for(p_d->get_parent());
			parts.push_back(cxx_name_from_die(p_d));
			return parts;
		}
		else return /*cxx_name_from_die(p_d);*/ local_name_parts_for(p_d);
		/* For simplicity, we want the fq names for base types to be
		 * their C++ keywords, not an alias. So if we're outputting
		 * a CU-toplevel DIE, defer to local_name_for -- we don't do
		 * this in the recursive case above because we might end up
		 * prefixing a C++ keyword with a namespace qualifier, which
		 * wouldn't compile (although presently would only happen in
		 * the strange circumstance of a non-CU-toplevel base type). */
	}
	
	bool 
	cxx_generator_from_dwarf::cxx_assignable_from(
		shared_ptr<spec::type_die> dest,
		shared_ptr<spec::type_die> source
	)
	{
		// FIXME: better approximation of C++ assignability rules goes here
		/* We say assignable if
		 * - base types (any), or
		 * - pointers to fq-nominally equal types, or
		 * - fq-nominally equal structured types _within the same dieset_ */

		if (dest->get_tag() == DW_TAG_base_type && source->get_tag() == DW_TAG_base_type)
		{ return true; }

		if (dest->get_tag() == DW_TAG_structure_type 
		&& source->get_tag() == DW_TAG_structure_type)
		{
			return fq_name_for(source) == fq_name_for(dest)
				&& &source->get_ds() == &dest->get_ds();
		}

		if (dest->get_tag() == DW_TAG_pointer_type
		&& source->get_tag() == DW_TAG_pointer_type)
		{
			if (!dynamic_pointer_cast<spec::pointer_type_die>(dest)->get_type())
			{
				return true; // can always assign to void
			}
			else return 
				dynamic_pointer_cast<spec::pointer_type_die>(source)
					->get_type()
				&& fq_name_for(dynamic_pointer_cast<spec::pointer_type_die>(source)
					->get_type()) 
					== 
					fq_name_for(dynamic_pointer_cast<spec::pointer_type_die>(dest)
					->get_type());
		}

		return false;
	}

	bool 
	cxx_generator_from_dwarf::cxx_is_complete_type(shared_ptr<spec::type_die> t)
	{
		if (t->get_tag() == DW_TAG_typedef && 
			!dynamic_pointer_cast<spec::typedef_die>(t)->get_type())
		{
			return false;
		}
		if (t->get_tag() == DW_TAG_array_type)
		{
			if (!dynamic_pointer_cast<spec::array_type_die>(t)
				->element_count()
			|| *dynamic_pointer_cast<spec::array_type_die>(t)
				->element_count() == 0)
			{
				return false;
			}
			else return true;
		}
		// if we're structured, we're complete iff all members are complete
		if (dynamic_pointer_cast<spec::with_named_children_die>(t))
		{
			auto nc = dynamic_pointer_cast<spec::with_named_children_die>(t);
			cerr << "DEBUG: testing completeness of cxx type for " << *t << endl;
			for (auto i_member = t->children_begin(); 
				i_member != t->children_end();
				i_member++)
			{
				if ((*i_member)->get_tag() != DW_TAG_member) continue;
				auto memb = dynamic_pointer_cast<spec::member_die>(*i_member);
				auto memb_opt_type = memb->get_type();
				if (!memb_opt_type)
				{
					return false;
				}
				if (!cxx_is_complete_type(memb_opt_type))
				{
					return false;
				}
			}
		}

		return true;
	}
	
	string 
	cxx_generator_from_dwarf::name_for_type(
		shared_ptr<spec::type_die> p_d, 
		boost::optional<const string&> infix_typedef_name /*= none*/,
		bool use_friendly_names/*= true*/)
	{
		return cxx_declarator_from_type_die(p_d, 
			infix_typedef_name,
			use_friendly_names);
	}	

	string 
	cxx_generator_from_dwarf::name_for_argument(
		shared_ptr<spec::formal_parameter_die> p_d, 
		int argnum
	)
	{
		std::ostringstream s;
		//std::cerr << "called name_for_argument on argument position " << argnum << ", ";
		if (p_d->get_name()) { /*std::cerr << "named ";*/ s << "_dwarfhpp_arg_" << *p_d->get_name(); /*std::cerr << *d.get_name();*/ }
		else { /*std::cerr << "anonymous";*/ s << "_dwarfhpp_anon_arg_" << argnum; }
		//std::cerr << std::endl;
		return s.str();
	} 

	string cxx_generator_from_dwarf::create_ident_for_anonymous_die(
		shared_ptr<spec::basic_die> p_d
	)
	{
		assert(!p_d->get_name());
		std::ostringstream s;
		s << "_dwarfhpp_anon_" << std::hex << p_d->get_offset();
		return s.str();
	}
	
	string 
	cxx_generator_from_dwarf::protect_ident(const string& ident)
	{
		/* In at least one reported case, the DWARF name of a declaration
		 * appearing in a standard header (pthread.h) conflicts with a macro (__WAIT_STATUS). 
		 * We could protect every ident with an #if defined(...)-type block, but
		 * that would make the header unreadable. Instead, we make a crude HACK
		 * of a guess: protect the ident if it uses a reserved identifier
		 * (for now: beginning '__')
		 * and is all caps (because most macros are all caps). */

		std::ostringstream s;
		if (ident.find("__") == 0 && ident == boost::to_upper_copy(ident))
		{
			s << std::endl << "#if defined(" << ident << ")" << std::endl
				<< "_dwarfhpp_protect_" << ident << std::endl
				<< "#else" << std::endl
				<< ident << std::endl
				<< "#define _dwarfhpp_protect_" << ident << " " << ident << std::endl
				<< "#endif" << std::endl;
		}
		else s << ident;
		return s.str();
	}
	
	string 
	cxx_generator_from_dwarf::make_typedef(
		shared_ptr<spec::type_die> p_d,
		const string& name
	)
	{
		std::ostringstream out;
		string name_to_use = cxx_name_from_string(name, "_dwarfhpp_");

		// make sure that if we're going to throw an exception, we do it before
		// we write any output.
		string declarator = name_for_type(p_d, name_to_use);
		out << "typedef " 
			<< protect_ident(declarator);
		// HACK: we use the infix for subroutine types
		if (!type_infixes_name(p_d->get_this()))
		{
			out << " "
				<< protect_ident(name_to_use);
		}
		out << ";" << endl;
		return out.str();
	}

	optional<string>
	cxx_target::name_for_base_type(shared_ptr<spec::base_type_die> p_d)
	{
		map<base_type, string>::iterator found = base_types.find(
			base_type(p_d));
		if (found == base_types.end()) return optional<string>();
		else return found->second;
	}

	const vector<string> cxx_generator::cxx_reserved_words = {
		"auto",
		"const",
		"double",
		"float",
		"int",
		"short",
		"struct",
		"unsigned",
		"break",
		"continue",
		"else",
		"for",
		"long",
		"signed",
		"switch",
		"void",
		"case",
		"default",
		"enum",
		"goto",
		"register",
		"sizeof",
		"typedef",
		"volatile",
		"char",
		"do",
		"extern",
		"if",
		"return",
		"static",
		"union",
		"while",
		"asm",
		"dynamic_cast",
		"namespace",
		"reinterpret_cast",
		"try",
		"bool",
		"explicit",
		"new",
		"static_cast",
		"typeid",
		"catch",
		"false",
		"operator",
		"template",
		"typename",
		"class",
		"friend",
		"private",
		"this",
		"using",
		"const_cast",
		"inline",
		"public",
		"throw",
		"virtual",
		"delete",
		"mutable",
		"protected",
		"true",
		"wchar_t",
		"and",
		"bitand",
		"compl",
		"not_eq",
		"or_eq",
		"xor_eq",
		"and_eq",
		"bitor",
		"not",
		"or",
		"xor"
	};

} // end namespace tool
} // end namespace dwarf
