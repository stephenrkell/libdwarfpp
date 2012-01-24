/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * cxx_model.hpp: tools for constructing binary-compatible C++ models 
 * 			of DWARF elements.
 *
 * Copyright (c) 2009--2012, Stephen Kell.
 */

#ifndef DWARFPP_CXX_MODEL_HPP_
#define DWARFPP_CXX_MODEL_HPP_

#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <sstream>
#include <boost/regex.hpp>

#include "cxx_compiler.hpp"

namespace dwarf {
namespace tool {

using namespace dwarf::lib;
using std::vector;
using std::map;
using std::string;
using boost::optional;
using boost::shared_ptr;

/** This class contains generally useful stuff for generating C++ code.
 *  It should be free from DWARF details. */
class cxx_generator
{
public:
	static const vector<string> cxx_reserved_words;
	static bool is_reserved(const string& word);
	static bool is_valid_cxx_ident(const string& word);
	virtual string make_valid_cxx_ident(const string& word);
	virtual string cxx_name_from_string(const string& s, const char *prefix);
	virtual string name_from_name_parts(const vector<string>& parts);
};

/** This class implements a mapping from DWARF constructs to C++ constructs,
 *  and utility functions for understanding the C++ constructs corresponding
 *  to various DWARF elements. */
class cxx_generator_from_dwarf : public cxx_generator
{
protected:
	virtual string get_anonymous_prefix()
	{ return "_dwarfhpp_anon_"; } // HACK: remove hard-coding of this in libdwarfpp, dwarfhpp and cake
	virtual string get_untyped_argument_typename() = 0;

public:
	const spec::abstract_def *const p_spec;

	cxx_generator_from_dwarf() : p_spec(&spec::DEFAULT_DWARF_SPEC) {}
	cxx_generator_from_dwarf(const spec::abstract_def& s) : p_spec(&s) {}


	bool is_builtin(shared_ptr<spec::basic_die> p_d);

	string 
	name_for(shared_ptr<spec::type_die> t) 
	{ return local_name_for(t); }
	
	virtual 
	optional<string>
	name_for_base_type(shared_ptr<spec::base_type_die>) = 0;
	
	vector<string> 
	name_parts_for(shared_ptr<spec::type_die> t) 
	{ return local_name_parts_for(t); }

	bool 
	type_infixes_name(shared_ptr<spec::basic_die> p_d);

	string 
	local_name_for(shared_ptr<spec::basic_die> p_d,
		bool use_friendly_names = true) 
	{ return name_from_name_parts(local_name_parts_for(p_d, use_friendly_names)); }

	vector<string> 
	local_name_parts_for(shared_ptr<spec::basic_die> p_d,
		bool use_friendly_names = true);
		
	string 
	fq_name_for(shared_ptr<spec::basic_die> p_d)
	{ return name_from_name_parts(fq_name_parts_for(p_d)); }
	
	vector<string> 
	fq_name_parts_for(shared_ptr<spec::basic_die> p_d);
	
	string 
	cxx_name_from_die(shared_ptr<spec::basic_die> p_d);

	bool 
	cxx_type_can_be_qualified(shared_ptr<spec::type_die> p_d);

	string 
	cxx_declarator_from_type_die(
		shared_ptr<spec::type_die> p_d, 
		optional<const string&> infix_typedef_name = optional<const string&>(),
		bool use_friendly_names = true,
		optional<const string&> extra_prefix = optional<const string&>(),
		bool use_struct_and_union_prefixes = true
	);

	bool 
	cxx_assignable_from(
		shared_ptr<spec::type_die> dest,
		shared_ptr<spec::type_die> source
	);

	bool 
	cxx_is_complete_type(shared_ptr<spec::type_die> t);
	
	string 
	name_for_type(
		shared_ptr<spec::type_die> p_d, 
		optional<const string&> infix_typedef_name = optional<const string&>(),
		bool use_friendly_names = true);

	string 
	name_for_argument(
		shared_ptr<spec::formal_parameter_die> p_d, 
		int argnum);

	string
	make_typedef(
		shared_ptr<spec::type_die> p_d,
		const string& name 
	);
	
	string
	make_function_declaration_of_type(
		shared_ptr<spec::subroutine_type_die> p_d,
		const string& name,
		bool write_semicolon = true
	);

	string 
	create_ident_for_anonymous_die(
		shared_ptr<spec::basic_die> p_d
	);

	string 
	protect_ident(const string& ident);
};

/** This class supports generation of C++ code targetting a particular C++ compiler. */
class cxx_target : public cxx_generator_from_dwarf, public cxx_compiler
{
public:
	// forward base constructors
	cxx_target(const vector<string>& argv) : cxx_compiler(argv) {}

	cxx_target(const spec::abstract_def& s, const vector<string>& argv)
	 : cxx_generator_from_dwarf(s), cxx_compiler(argv) {}

	cxx_target(const spec::abstract_def& s)
	 : cxx_generator_from_dwarf(s) {}

	cxx_target() {}
	
	// implementation of pure virtual function in cxx_generator_from_dwarf
	optional<string> name_for_base_type(shared_ptr<spec::base_type_die> p_d);
};

} // end namespace tool
} // end namespace dwarf

#endif
