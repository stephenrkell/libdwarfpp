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
using std::deque;
using boost::optional;
using std::shared_ptr;
using std::dynamic_pointer_cast;
using namespace dwarf::spec;
using dwarf::lib::Dwarf_Half;
using dwarf::lib::Dwarf_Off;
using dwarf::lib::Dwarf_Unsigned;

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
		static const std::regex e("[a-zA-Z_][a-zA-Z0-9_]*");
		return !is_reserved(word) &&
			std::move(regex_match(word, e));	
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
		//cerr << "Does this type infix a name? " << *t << endl;
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
		/* In C code, we can get a problem with tagged namespaces (struct, union, enum) 
		 * overlapping with member names (and with each other). This causes an error like
		 * error: declaration of `cake::link_p2k_::kfs::uio_rw cake::link_p2k_::kfs::uio::uio_rw'
		 * librump.o.hpp:1341:6: error: changes meaning of `uio_rw' from `enum cake::link_p2k_::kfs::uio_rw'
		 *
		 * We work around it by using the anonymous DIE name if there is a CU-toplevel 
		 * decl of this name (HACK: should really be visible file-toplevel).
		 */
		
		// to make sure we don't get ourselves as "conflicting",
		// we should check that we're a member_die or other non-CU-level thing
		auto conflicting_toplevel_die = 
			(p_d->get_name() && p_d->get_parent()->get_tag() != DW_TAG_compile_unit) 
				? p_d->get_ds().toplevel()->visible_named_grandchild(*p_d->get_name())
				: shared_ptr<spec::basic_die>();
		assert(!conflicting_toplevel_die
		|| conflicting_toplevel_die->iterator_here() != p_d->iterator_here());
		string name_to_use;
		if (p_d->get_name() && !conflicting_toplevel_die)
		{
			name_to_use = cxx_name_from_string(*p_d->get_name(), "_dwarfhpp_");
			return name_to_use;
		}
		else if (p_d->get_name())
		{
			assert(conflicting_toplevel_die);
			// this is the conflicting case
			cerr << "Warning: renaming element " << *p_d->get_name()
				<< " child of " << p_d->get_parent()->summary()
				<< " because it conflicts with toplevel " << conflicting_toplevel_die->summary()
				<< endl;
			name_to_use = create_ident_for_anonymous_die(p_d);
			return name_to_use;
		}
		else // anonymous case
		{
			ostringstream s;
			s << "_dwarfhpp_anon_" << hex << p_d->get_offset();
			return s.str();
		}
	}
	
	bool cxx_generator_from_dwarf::cxx_type_can_be_qualified(shared_ptr<spec::type_die> p_d) const
	{
		if (p_d->get_tag() == DW_TAG_array_type) return false;
		if (p_d->get_tag() == DW_TAG_subroutine_type) return false;
		// FIXME: any more?
		// now we know this bit of the type is fine for qualifying 
		// what about chained bits?
		if (!dynamic_pointer_cast<type_chain_die>(p_d)) return true;
		// if we're chaining void, also true
		if (!dynamic_pointer_cast<type_chain_die>(p_d)->get_type()) return true;
		return cxx_type_can_be_qualified(
			dynamic_pointer_cast<type_chain_die>(p_d)->get_type()
		);
	}

	bool cxx_generator_from_dwarf::cxx_type_can_have_name(shared_ptr<spec::type_die> p_d) const
	{
		/* In C++, introducing a name to a modified type requires typedef. */
		if (p_d->get_tag() == DW_TAG_typedef) return true;
		else if (dynamic_pointer_cast<type_chain_die>(p_d)) return false;
		else return true;
	}

	pair<string, bool>
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
				return make_pair(
					((extra_prefix && !use_friendly_names) ? *extra_prefix : "")
					+ local_name_for(dynamic_pointer_cast<spec::base_type_die>(p_d),
						use_friendly_names),
					false);
			case DW_TAG_typedef:
				return make_pair((extra_prefix ? *extra_prefix : "") + *p_d->get_name(), false);
			case DW_TAG_reference_type: {
				shared_ptr<spec::reference_type_die> reference 
				 = dynamic_pointer_cast<spec::reference_type_die>(p_d);
				assert(reference);
				assert(reference->get_type());
				auto declarator = cxx_declarator_from_type_die(
						reference->get_type(), optional<const string&>(),
						use_friendly_names, extra_prefix, 
							use_struct_and_union_prefixes);
				return make_pair(declarator.first + "&", declarator.second);
			}
			case DW_TAG_pointer_type: {
				shared_ptr<spec::pointer_type_die> pointer 
				 = dynamic_pointer_cast<spec::pointer_type_die>(p_d);
				if (pointer->get_type())
				{
//					if (pointer->get_type()->get_tag() == DW_TAG_subroutine_type)
//					{
//						// we have a pointer to a subroutine type -- pass on the infix name
						auto declarator = cxx_declarator_from_type_die(
							pointer->get_type(), 
							infix_typedef_name ? ("*" + *infix_typedef_name) : optional<const string&>(), 
							use_friendly_names, extra_prefix, 
							use_struct_and_union_prefixes);
						if (!declarator.second) return make_pair(declarator.first + "*", false);
						else return make_pair(declarator.first, true);
						//return make_pair(declarator.first + "*", declarator.second);
// 					}
// 					else 
// 					{
// 						// Q. Why don't we pass on the infix name here too?
// 						auto declarator = cxx_declarator_from_type_die(
// 							pointer->get_type(), optional<const string&>(),
// 							use_friendly_names, extra_prefix, 
// 							use_struct_and_union_prefixes);
// 						return make_pair(declarator.first + "*", declarator.second);
// 					}
				}
				else return make_pair("void *", false);
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
				return make_pair(cxx_declarator_from_type_die(arr->get_type(), 
							optional<const string&>(), 
							use_friendly_names, extra_prefix, 
							use_struct_and_union_prefixes).first
					+ " " + (infix_typedef_name ? *infix_typedef_name : "") + "[" 
					// add size, if we have a subrange type
					+ arrsize.str()
					+ "]", infix_typedef_name ? true : false);
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
					).first 
					: string("void "));
				s << "(" << (infix_typedef_name ? *infix_typedef_name : "")
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
										).first;
								break;
							case DW_TAG_unspecified_parameters:
								s << "...";
								break;
							default: assert(false); break;
						}
					}
				}
				catch (lib::No_entry) { s << ")"; }
				return make_pair(s.str(), true);
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
				return make_pair(
					(extra_prefix ? *extra_prefix : "") + name_prefix + cxx_name_from_die(p_d),
					false);
			handle_qualified_type: {
				/* This is complicated by the fact that array types in C/C++ can't be qualified directly,
				 * but such qualified types can be defined using typedefs. (FIXME: I think this is correct
				 * but don't quote me -- there might just be some syntax I'm missing.) */
				auto chained_type =  dynamic_pointer_cast<spec::type_chain_die>(p_d)->get_type();
				/* Note that many DWARF emitters record C/C++ "const void" (as in "const void *")
				 * as a const type with no "type" attribute. So handle this case. */
				if (!chained_type) return make_pair("void" + qualifier_suffix, false);
				else if (cxx_type_can_be_qualified(chained_type))
				{
					auto declarator = cxx_declarator_from_type_die(
						chained_type, 
						infix_typedef_name,
						use_friendly_names, extra_prefix, 
						use_struct_and_union_prefixes
						);
					return make_pair(declarator.first + qualifier_suffix, declarator.second);
				}
				else 
				{
					// fallback: assume that the more primitive (unqualified) type
					// already has a typedef. So we just qualify that typedef and return.
					return make_pair(create_ident_for_anonymous_die(chained_type)
						+ " " + qualifier_suffix, false);

					//throw Not_supported(string("C++ qualifiers for types of tag ")
					//	 + p_d->get_spec().tag_lookup(chained_type->get_tag()));
				}
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
		t = t->get_concrete_type();
		if (!t) return false;
		
// 		// if we can't find a definition, we're not complete
// 		if (t->get_declaration() && *t->get_declaration()) 
// 		{
// 			auto with_data_members = dynamic_pointer_cast<with_data_members_die>(t);
// 			if (with_data_members)
// 			{
// 				auto defn = with_data_members->find_my_own_definition();
// 				if (!defn) return false;
// 			}
// 			else assert(false); // FIXME: do we get decls for other kinds of DIE?
// 		}
		// HACK: For now, if we're *not* a definition, we're not complete.
		// It's the caller's problem to find a definition if one exists.
		if (t->get_declaration() && *t->get_declaration()) 
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
		// if we're structured, we're complete iff 
		// we don't have the "declaration" flag
		// and all members are complete
		if (dynamic_pointer_cast<spec::with_named_children_die>(t))
		{
			auto nc = dynamic_pointer_cast<spec::with_named_children_die>(t);
			//cerr << "DEBUG: testing completeness of cxx type for " << *t << endl;
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
	
	pair<string, bool>
	cxx_generator_from_dwarf::name_for_type(
		shared_ptr<spec::type_die> p_d, 
		boost::optional<const string&> infix_typedef_name /*= none*/,
		bool use_friendly_names/*= true*/)
	{
// 		try
// 		{
			return cxx_declarator_from_type_die(p_d, 
				infix_typedef_name,
				use_friendly_names);
// 		} 
// 		catch (dwarf::lib::Not_supported)
// 		{
// 			// HACK around this strange (const (array)) case
// 			if (p_d->get_tag() == DW_TAG_const_type
// 			 && dynamic_pointer_cast<const_type_die>(p_d)->get_type()->get_concrete_type()->get_tag()
// 			 	== DW_TAG_array_type)
// 			{
// 				// assume that we will be emitting a typedef for the const type itself,
// 				// so just output its anonymous name.
// 				// BUT how did we emit that typedef? We would run down this same path.
// 				return make_pair(create_ident_for_anonymous_die(p_d), false);
// 			}
// 			else throw;
// 		}
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
		auto declarator = name_for_type(p_d, name_to_use);
		out << "typedef " 
			<< protect_ident(declarator.first);
		// HACK: we use the infix for subroutine types
		if (!declarator.second)
		{
			out << " "
				<< protect_ident(name_to_use);
		}
		out << ";" << endl;
		return out.str();
	}
	
	string
	cxx_generator_from_dwarf::make_function_declaration_of_type(
		shared_ptr<spec::subroutine_type_die> p_d,
		const string& name,
		bool write_semicolon /* = true */,
		bool wrap_with_extern_lang /* = true */
	)
	{
		std::ostringstream out;
		string name_to_use = cxx_name_from_string(name, "_dwarfhpp_");
		
		string lang_to_use;
		switch (p_d->enclosing_compile_unit()->get_language())
		{
			case DW_LANG_C:
			case DW_LANG_C89:
			case DW_LANG_C99:
				lang_to_use = "C";
				break;
			default: 
				assert(false);
				wrap_with_extern_lang = false;
				break;
		}
		
		if (wrap_with_extern_lang)
		{
			out << "extern \"" << lang_to_use << "\" {\n";
		}
		
		if (p_d->get_type()) out << name_for_type(p_d->get_type()).first;
		else out << "void";
		out << " " << name << "(";
		
		bool written_an_arg = false;
		for (auto i_fp = p_d->formal_parameter_children_begin();
			i_fp != p_d->formal_parameter_children_end();
			++i_fp)
		{
			if (i_fp != p_d->formal_parameter_children_begin()) out << ", ";
			
			if ((*i_fp)->get_type())
			{
				auto declarator = ((*i_fp)->get_name())
					? name_for_type((*i_fp)->get_type(), *(*i_fp)->get_name())
					: name_for_type((*i_fp)->get_type());
				out << declarator.first;
				if (!declarator.second) out << " " 
					<< ((*i_fp)->get_name() ? *(*i_fp)->get_name() : "");
				else out << "void *" << ((*i_fp)->get_name() ? *(*i_fp)->get_name() : "");
				written_an_arg = true;
			}
		}
		if (p_d->unspecified_parameters_children_begin() != 
			p_d->unspecified_parameters_children_end())
		{
			if (written_an_arg) out << ", ";
			out << "...";
		}
		out << ")";
		if (write_semicolon) out << ";";
		out << endl;
		if (write_semicolon && wrap_with_extern_lang) 
		{
			out << "} // end extern " << lang_to_use << endl;
		}
		
		return out.str();
	}
	
	template <Dwarf_Half Tag>
	void 
	cxx_generator_from_dwarf::emit_model(
		srk31::indenting_ostream& out,
		abstract_dieset::iterator i_d
	)
	{
		assert((*i_d)->get_tag() == Tag);
		out << "// FIXME: DIEs of tag " << Tag << endl;
		return;
	}
		
	template <typename Pred>
	void 
	cxx_generator_from_dwarf::recursively_emit_children(
		indenting_ostream& out,
		abstract_dieset::iterator i_d,
		const Pred& pred /* = Pred() */
	)
	{ 
		auto p_d = *i_d;
		//out.inc_level(); 
		bool not_yet_inced = true; // we delay inc'ing because it emits a newline...
		for (dwarf::spec::abstract_dieset::iterator i = p_d->children_begin(); 
				i != p_d->children_end(); ++i)
		{
			// if the caller asked only for a subset of children, we oblige
			if (!pred(*i)) continue;
			
			// ... and we don't want the newline uness we actually have children
			if (not_yet_inced) { out.inc_level(); not_yet_inced = false; }
			
			// emit_mode<0> does a dynamic dispatch on the tag
			// (could really be a separate function, rather than the <0> specialization)
			dispatch_to_model_emitter(out, i);
		}
		if (!not_yet_inced) out.dec_level(); 
	}

	// define specializations here
	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_base_type>             (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<base_type_die>(*i_d);
		optional<string> type_name_in_compiler = 
			name_for_base_type(p_d);

		if (!type_name_in_compiler) return; // FIXME: could define a C++ ADT!

		auto our_name_for_this_type = name_for_type(
			dynamic_pointer_cast<spec::type_die>(p_d), 
			0 /* no infix */, 
			false /* no friendly names*/);
		assert(!our_name_for_this_type.second);

		if (our_name_for_this_type.first != *type_name_in_compiler)
		{
			out << "typedef " << *type_name_in_compiler
				<< ' ' << protect_ident(our_name_for_this_type.first)
				<< ';' << endl;
		}
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_subprogram>            (indenting_ostream& out, abstract_dieset::iterator i_d) 
	{
		auto p_d = dynamic_pointer_cast<subprogram_die>(*i_d);
		
		// skip unnamed, for now (FIXME)
		if (!p_d->get_name() || (*p_d->get_name()).empty()) return;

		// wrap with extern "lang"
		switch(dynamic_pointer_cast<compile_unit_die>(
			p_d->enclosing_compile_unit())->get_language())
		{
			case DW_LANG_C:
			case DW_LANG_C89:
			case DW_LANG_C99:
				if (p_d->get_calling_convention() && *p_d->get_calling_convention() != DW_CC_normal)
				{
					cerr << "Warning: skipping subprogram with nonstandard calling convention: "
						<< *p_d << endl;
					return;
				}
				out << "extern \"C\" { /* C-language subprogram DIE at 0x" 
					<< std::hex << p_d->get_offset() << std::dec << " */" << endl;
				break;
			default:
				assert(false);
		}

		out  << (p_d->get_type() 
					? protect_ident(name_for_type(p_d->get_type()).first)
					: std::string("void")
				)
			<< ' '
			<< *p_d->get_name()
			<< '(';		

		/* recurse on children -- we do children in a particular order!
		 * formal parameters,
		 * then unspecified parameters,
		 * then others. */
		recursively_emit_children(out, i_d, 
			[](shared_ptr<basic_die> p){ return p->get_tag() == DW_TAG_formal_parameter; }
		);
		recursively_emit_children(out, i_d, 
			[](shared_ptr<basic_die> p){ return p->get_tag() == DW_TAG_unspecified_parameters; }
		);
		recursively_emit_children(out, i_d, 
			[](shared_ptr<basic_die> p){ 
				return p->get_tag() != DW_TAG_unspecified_parameters
				    && p->get_tag() != DW_TAG_formal_parameter; }
		);
		
		// end the prototype
		out	<< ");";

		// close the extern block
		out << "}" << std::endl;
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_formal_parameter>      (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<spec::formal_parameter_die>(*i_d);
	
		// recover arg position
		int argpos = 0;
		shared_ptr<subprogram_die> p_subp = dynamic_pointer_cast<subprogram_die>(
			(*i_d)->get_parent());

		auto i = p_subp->formal_parameter_children_begin();
		while (i != p_subp->formal_parameter_children_end() 
			&& (*i)->get_offset() != (*i_d)->get_offset())
		{ argpos++; i++; }

		assert(i != p_subp->formal_parameter_children_end());

		if (argpos != 0) out << ", ";

		out	<< (p_d->get_type() 
				? protect_ident(name_for_type(transform_type(p_d->get_type(), i_d)).first)
				: get_untyped_argument_typename()
				)
			<< ' '
			<< protect_ident(name_for_argument(p_d, argpos));
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_unspecified_parameters>(indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<unspecified_parameters_die>(*i_d);
		
		// were there any specified args?
		shared_ptr<subprogram_die> p_subp
		 = dynamic_pointer_cast<subprogram_die>(p_d->get_parent());

		if (p_subp->formal_parameter_children_begin()
			 != p_subp->formal_parameter_children_end())
		{
			out << ", ";
		}
		
		out << "...";
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_array_type>            (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		out << make_typedef(
			dynamic_pointer_cast<spec::type_die>(*i_d),
			create_ident_for_anonymous_die(*i_d)
		);
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_enumeration_type>      (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		out << "enum " 
			<< protect_ident(
				((*i_d)->get_name() 
					? *(*i_d)->get_name() 
					: create_ident_for_anonymous_die(*i_d)
				)
			)
			<< " { " << std::endl;

		recursively_emit_children(out, i_d);

		out << endl << "};" << endl;
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_member>                (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<member_die>(*i_d);
		
		/* To reproduce the member's alignment, we always issue an align attribute. 
		 * We choose our alignment so as to ensure that the emitted field is located
		 * at the offset specified in DWARF. */
		shared_ptr<type_die> member_type = transform_type(p_d->get_type(), i_d);

		// recover the previous formal parameter's offset and size
		shared_ptr<spec::with_data_members_die> p_type = 
			dynamic_pointer_cast<spec::with_data_members_die>(p_d->get_parent());
		assert(p_type);
		auto i = p_type->member_children_begin();
		auto prev_i = p_type->member_children_end();
		while (i != p_type->member_children_end() 
			&& (*i)->get_offset() != p_d->get_offset()) 
		{ prev_i = i; ++i; }
		
		// now i points to us, and prev_i to our predecessor
		assert(i != p_type->member_children_end()); // would mean we failed to find ourselves

		Dwarf_Unsigned cur_offset;
		if (prev_i == p_type->member_children_end()) cur_offset = 0;
		else 
		{
			shared_ptr<member_die> prev_member = dynamic_pointer_cast<member_die>(*prev_i);
			assert(prev_member->get_type());

			if (prev_member->get_data_member_location())
			{
				auto prev_member_calculated_byte_size 
				 = transform_type(prev_member->get_type(), i_d)->calculate_byte_size();
				if (!prev_member_calculated_byte_size)
				{
					cerr << "couldn't calculate size of data member " << prev_member
						<< " so giving up control of layout in struct " << *p_type
						<< endl;
					cur_offset = std::numeric_limits<Dwarf_Unsigned>::max(); // sentinel value
				}
				else
				{
					cur_offset = evaluator(
						prev_member->get_data_member_location()->at(0), 
						p_d->get_ds().get_spec(),
						stack<Dwarf_Unsigned>(
							// push zero as the initial stack value
							deque<Dwarf_Unsigned>(1, 0UL)
							)
						).tos()
						+ *prev_member_calculated_byte_size; 
				}
			}
			else
			{
				cerr << "no data member location: context die is " << *p_d->get_parent() << endl;
				cur_offset = (p_d->get_parent()->get_tag() == DW_TAG_union_type) 
					? 0 : std::numeric_limits<Dwarf_Unsigned>::max();
			}
		}

		/* This handles the difficulty where in C code, we can get a problem
		 * with tagged namespaces (struct, union, enum) 
		 * overlapping with member names (and with each other).
		 */
		string name_to_use = cxx_name_from_die(p_d);

		auto declarator = name_for_type(member_type, name_to_use);
		// (p_d->get_name())
		//	? name_for_type(member_type, *p_d->get_name())
		//	: name_for_type(member_type, optional<const string&>());
		
		out << protect_ident(declarator.first);
		out	<< " ";
		if (!declarator.second)
		{
			out << protect_ident(name_to_use);
		}

		if (p_d->get_data_member_location() && p_d->get_data_member_location()->size() == 1)
		{
			Dwarf_Unsigned target_offset = evaluator(
				p_d->get_data_member_location()->at(0), 
				p_d->get_ds().get_spec(),
				stack<Dwarf_Unsigned>(
					// push zero as the initial stack value
					deque<Dwarf_Unsigned>(1, 0UL)
					)
				).tos();

			if (!(target_offset > 0 && cur_offset != std::numeric_limits<Dwarf_Unsigned>::max()))
			{
				out << ";";
			}
			else
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
				const unsigned MAXIMUM_SANE_OFFSET = 1<<30; // 1GB
				unsigned test_offset = cur_offset;
				do
				{
					// if it doesn't divide by our power of two,
					// set it to the next-highest multiple of our power
					if (test_offset % power != 0)
					{
						test_offset = ((test_offset / power) + 1) * power;
					}
					assert(test_offset % power == 0);
					// now we have the offset we'd get if we chose aligned(power)
					
					// HMM: what if test_offset
					// also divides by the *next* power of two?
					// In that case, we will go straight through.
					// Eventually we will get a power that is bigger than it.
					// Then it won't be divisible.
					
					// now test_offset is a multiple of power -- check it's our desired offset
					is_good = (test_offset == target_offset);
				} while (!is_good && (power <<= 1, power != 0));
				
				if (power == 0 || test_offset < cur_offset)
				{
					// This happens for weird object layouts, i.e. with gaps in.
					// Test case: start 48, target 160; the relevant power 
					// cannot be higher than 32, because that's the highest that
					// divides 160. But if we choose align(32), we will get offset 64.
					out << ";" << endl;
					out << "// WARNING: could not infer alignment for offset "
						<< target_offset << " starting at " << cur_offset << endl;
					if (target_offset - cur_offset < MAXIMUM_SANE_OFFSET)
					{
						out << "char " << create_ident_for_anonymous_die(p_d) << "_padding["
							<< target_offset - cur_offset << "];" << endl;
					}
					else
					{
						out << "// FIXME: this struct is WRONG (probably until we support bitfields)" << endl;
					}
					power = 1;
				}
					
				out << " __attribute__((aligned(" << power << ")));";
			}
				/*<< " static_assert(offsetof(" 
				<< name_for_type(compiler, p_type, boost::optional<const std::string&>())
				<< ", "
				<< protect_ident(*d.get_name())
				<< ") == " << offset << ");" << */
			out << " // offset: " << target_offset << endl;
		}
		else 
		{
			// guess at word alignment
			out << " __attribute__((aligned(sizeof(int))))"; 
			out << "; // no DW_AT_data_member_location, so it's a guess" << std::endl;
		}
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_pointer_type>          (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<pointer_type_die>(*i_d);
		
		// we always emit a typedef with synthetic name
		// (but the user could just use the pointed-to type and "*")

		// std::cerr << "pointer type: " << d << std::endl;
		//assert(!d.get_name() || (d.get_type() && d.get_type()->get_tag() == DW_TAG_subroutine_type));

		std::string name_to_use
		 = p_d->get_name() 
		 ? cxx_name_from_string(*p_d->get_name(), "_dwarfhpp_") 
		 : create_ident_for_anonymous_die(p_d);

		if (!p_d->get_type())
		{
			out << "typedef void *" 
				<< protect_ident(name_to_use) 
				<< ";" << std::endl;
		}
		else 
		{
			out << make_typedef(dynamic_pointer_cast<spec::type_die>(p_d), name_to_use);
		}
	}
	
	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_reference_type>        (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<reference_type_die>(*i_d);
		
		// we always emit a typedef with synthetic name
		// (but the user could just use the pointed-to type and "*")

		// std::cerr << "pointer type: " << d << std::endl;
		//assert(!d.get_name() || (d.get_type() && d.get_type()->get_tag() == DW_TAG_subroutine_type));

		std::string name_to_use
		 = p_d->get_name() 
		 ? cxx_name_from_string(*p_d->get_name(), "_dwarfhpp_") 
		 : create_ident_for_anonymous_die(p_d);

		if (!p_d->get_type())
		{
			out << "typedef void *" 
				<< protect_ident(name_to_use) 
				<< ";" << std::endl;
		}
		else 
		{
			out << make_typedef(dynamic_pointer_cast<spec::type_die>(p_d), name_to_use);
		}
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_structure_type>        (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<structure_type_die>(*i_d);
		
		out << "struct " 
			<< protect_ident(
				p_d->get_name() 
				?  *p_d->get_name() 
				: create_ident_for_anonymous_die(p_d->shared_from_this())
				);
		if (!(p_d->get_declaration() && *p_d->get_declaration()))
		{
			out << " { " << std::endl;

			recursively_emit_children(out, i_d);

			out << "}"; // we used to do __attribute__((packed)) here....
		}
		else
		{
			/* struct declarations shouldn't have any member children */
			assert(srk31::count(p_d->member_children_begin(), p_d->member_children_end())
				== 0);
		}
		out << ";" << std::endl;
	}
	
	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_subroutine_type>       (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		std::cerr << "Warning: assuming subroutine type at 0x" << std::hex << (*i_d)->get_offset() 
			<< std::dec << " is the target of some pointer type or typedef; skipping." << std::endl;
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_typedef>               (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<typedef_die>(*i_d);
		assert(p_d->get_name());
		if (!p_d->get_type())
		{
			std::cerr << "Warning: using `void' for typeless typedef: " << *p_d << std::endl;		
			out << "typedef void " << protect_ident(*p_d->get_name()) << ";" << std::endl;
			return;
		}
		out << make_typedef(transform_type(p_d->get_type(), i_d),
			*p_d->get_name() 
		);
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_union_type>            (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<union_type_die>(*i_d);
		
		/* FIXME: this needs the same treatment of forward declarations
		 * that structure_type gets. */
		
		out << "union " 
			<< protect_ident(
				p_d->get_name() 
				? *p_d->get_name() 
				: create_ident_for_anonymous_die(p_d)
				)
			<< " { " << std::endl;

		recursively_emit_children(out, i_d);

		out << "};" << std::endl;
	}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_const_type>            (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<const_type_die>(*i_d);
		
		// we always emit a typedef with synthetic name
		// (but the user could just use the pointed-to type and "const")

		try
		{
			out << make_typedef(
				dynamic_pointer_cast<spec::type_die>(p_d),
				create_ident_for_anonymous_die(p_d)
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

			out << "typedef " << create_ident_for_anonymous_die(p_d->get_type())
				<< " const " << create_ident_for_anonymous_die(p_d) 
				<< ";" << endl;
		}
	}
	
	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_constant>              (indenting_ostream& out, abstract_dieset::iterator i_d)
	{}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_enumerator>            (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<enumerator_die>(*i_d);
		
		// FIXME
		if ((*dynamic_pointer_cast<enumeration_type_die>(p_d->get_parent())
			->enumerator_children_begin())
				->get_offset() != p_d->get_offset()) // .. then we're not the first, so
				out << ", " << endl;
		out << protect_ident(*p_d->get_name());
	}
	
	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_variable>              (indenting_ostream& out, abstract_dieset::iterator i_d)
	{}

	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_volatile_type>         (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<volatile_type_die>(*i_d);
		// we always emit a typedef with synthetic name
		// (but the user could just use the pointed-to type and "const")

		try
		{
			out << make_typedef(
				dynamic_pointer_cast<spec::type_die>(p_d),
				create_ident_for_anonymous_die(p_d)
			);
		}
		catch (dwarf::lib::Not_supported)
		{
			/* See note for const_type above. */

			out << "typedef " << create_ident_for_anonymous_die(p_d->get_type())
				<< " volatile " << create_ident_for_anonymous_die(p_d) 
				<< ";" << endl;
		}
	}
	
	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_restrict_type>         (indenting_ostream& out, abstract_dieset::iterator i_d)
	{}
	
	template<> void cxx_generator_from_dwarf::emit_model<DW_TAG_subrange_type>         (indenting_ostream& out, abstract_dieset::iterator i_d)
	{
		auto p_d = dynamic_pointer_cast<subrange_type_die>(*i_d);
		// Since we can't express subranges directly in C++, we just
		// emit a typedef of the underlying type.
		out << "typedef " 
			<< protect_ident(name_for_type( 
				dynamic_pointer_cast<spec::type_chain_die>(p_d)->get_type()).first
				)
			<< " " 
			<< protect_ident(
				p_d->get_name() 
				? *p_d->get_name() 
				: create_ident_for_anonymous_die(p_d)
				)
			<< ";" << std::endl;
	}

/* from dwarf::tool::cxx_target */
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
