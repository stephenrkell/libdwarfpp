#include "cxx_compiler.hpp"

#include <cstdlib>
#include <sstream>
#include <fstream>
#include <string>
#include <cstring>
#include <map>

#include "adt.hpp"

using std::dynamic_pointer_cast;
using std::shared_ptr;
using boost::optional;
using std::string;
using std::vector;
using std::istringstream;
using std::ostringstream;
using std::pair;
using std::make_pair;
using std::map;
using std::multimap;
using std::cerr;
using std::endl;

namespace dwarf { namespace tool {
	cxx_compiler::cxx_compiler() : compiler_argv(default_compiler_argv()) 
	{
		discover_base_types();
	}
	cxx_compiler::cxx_compiler(const vector<string>& argv)
		: 	compiler_argv(argv)
	{
		discover_base_types();
	}
	
	/* We put first the ones that GCC's DWARF generator seems to use.
	 * This is helpful in libcrunch, to avoid defining too many uniqtype symbol aliases, 
	 * although eventually we should define the full set anyway. */
	const char *cxx_compiler::base_typename_equivs_schar[] = {
		"signed char",
		"char",
		"char signed",
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_uchar[] = {
		"unsigned char",
		"char unsigned",
		NULL
	}; 
	const char *cxx_compiler::base_typename_equivs_sshort[] = {
		"short int",
		"short",
		"int short",
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_ushort[] = {
		"short unsigned int",
		"unsigned short",
		"short unsigned",
		"unsigned short int",
		"int unsigned short",
		"int short unsigned",
		"unsigned int short",
		"short int unsigned",
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_sint[] = {
		"int",
		"signed", 
		"signed int",
		"int signed",
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_uint[] = {
		"unsigned int",
		"unsigned",
		"int unsigned",
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_slong[] = {
		"long int", 
		"long",
		"int long",
		"signed long int", // with {int, signed}
		"int signed long",
		"int long signed",
		"long signed int",
		"signed int long",
		"long signed", // with signed
		"signed long", 
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_ulong[] = {
		"unsigned long int", // with {int, signed}
		"int unsigned long",
		"int long unsigned",
		"long unsigned int",
		"unsigned int long",
		"long unsigned", // with signed
		"unsigned long", 
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_slonglong[] = {
		"long long int",
		"long long",
		"long int long",
		"int long long",
		"long long signed", // with signed
		"long signed long",
		"signed long long",
		"long long int signed", // with {int, signed} -- int at end
		"long long signed int",
		"long signed long int",
		"signed long long int",
		"long int long signed", // with {int, signed} -- int moving left
		"long int signed long",
		"long signed int long",
		"signed long int long",
		"int long long signed",  // with {int, signed} -- int moving left again
		"int long signed long",
		"int signed long long",
		"signed int long long",
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_ulonglong[] = {
		"long long unsigned int",
		"long long unsigned", // with signed
		"long unsigned long",
		"unsigned long long",
		"long long int unsigned", // with {int, signed} -- int at end
		"long unsigned long int",
		"unsigned long long int",
		"long int long unsigned", // with {int, signed} -- int moving left
		"long int unsigned long",
		"long unsigned int long",
		"unsigned long int long",
		"int long long unsigned",  // with {int, signed} -- int moving left again
		"int long unsigned long",
		"int unsigned long long",
		"unsigned int long long",
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_float[] = {
		"float",
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_double[] = {
		"double",
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_long_double[] = {
		"long double",
		"double long",
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_bool[] = {
		"bool",	
		NULL
	};
	const char *cxx_compiler::base_typename_equivs_wchar_t[] = {
		"wchar_t",
		NULL
	};

	const char **cxx_compiler::base_typename_equivs[] = {
		cxx_compiler::base_typename_equivs_schar, 
		cxx_compiler::base_typename_equivs_uchar, 
		cxx_compiler::base_typename_equivs_sshort, 
		cxx_compiler::base_typename_equivs_ushort, 
		cxx_compiler::base_typename_equivs_sint, 
		cxx_compiler::base_typename_equivs_uint, 
		cxx_compiler::base_typename_equivs_slong, 
		cxx_compiler::base_typename_equivs_ulong, 
		cxx_compiler::base_typename_equivs_slonglong, 
		cxx_compiler::base_typename_equivs_ulonglong, 
		cxx_compiler::base_typename_equivs_float, 
		cxx_compiler::base_typename_equivs_double, 
		cxx_compiler::base_typename_equivs_long_double, 
		cxx_compiler::base_typename_equivs_bool, 
		cxx_compiler::base_typename_equivs_wchar_t, 
		NULL
	};
	
	void cxx_compiler::discover_base_types()
	{
		/* Discover the DWARF descriptions of our compiler's base types.
		 * - Output and compile a test program generating all the base types
		 * we know of.
		 * - Compile it to a temporary file, read the DWARF and build a map
		 * of descriptions to compiler-builtin-typenames.
		 * - FIXME: it'd be nice if we could account for compiler flags etc.. */

		// We now try to make these names exhaustive.
		ostringstream test_src;

		vector<string> base_typenames_vec;
		for (auto p_equiv = base_typename_equivs[0]; p_equiv != NULL; ++p_equiv)
		{
			for (auto p_el = p_equiv[0]; p_el != NULL; ++p_el)
			{
				base_typenames_vec.push_back(string(p_el));
			}
		}
		
		//(&base_typenames[0],
		//	&base_typenames[sizeof base_typenames / sizeof (const char *) - 1]);

		int funcount = 0;
		for (vector<string>::iterator i_tn = base_typenames_vec.begin();
				i_tn != base_typenames_vec.end(); ++i_tn)
		{
			// prototype
			test_src << "void foo_" << funcount++
				<< "(" << *i_tn << " arg);";
			// definition
			test_src << "void foo_" << funcount++
				<< "(" << *i_tn << " arg) {};" << endl;
		}

		const char template_string[] = "/tmp/tmp.XXXXXX";
		char tmpnam_src_outfile_buf[sizeof template_string];
		strcpy(tmpnam_src_outfile_buf, template_string);

		int mkstemp_return = mkstemp(tmpnam_src_outfile_buf);
		assert(mkstemp_return != -1);
		std::ofstream test_src_outfile(tmpnam_src_outfile_buf);

		const string tmp_s = test_src.str();
		test_src_outfile << tmp_s.c_str();
		test_src_outfile.close();

		ostringstream cmdstream;
		char tmpnam_cxxoutput_buf[sizeof template_string];
		strcpy(tmpnam_cxxoutput_buf, template_string);
		
		mkstemp_return = mkstemp(tmpnam_cxxoutput_buf);
		assert(mkstemp_return != -1);
		
		// add compiler command+args to cmd
		vector<string> cmd = compiler_argv;
		cmd.push_back("-g"); 
 		cmd.push_back("-c"); 
		cmd.push_back("-o");
		cmd.push_back(string(tmpnam_cxxoutput_buf));
		cmd.push_back("-x c++");
		cmd.push_back(string(tmpnam_src_outfile_buf));
		
		for (vector<string>::iterator i_arg = cmd.begin();
				i_arg != cmd.end(); ++i_arg) cmdstream << *i_arg << ' ';
		
		cerr << "About to execute: " << cmdstream.str().c_str() << endl;
		int retval = system(cmdstream.str().c_str());
		assert(retval != -1);

		// now read the file that we generated
		FILE* f = fopen(tmpnam_cxxoutput_buf, "r");
		assert(f);

		// encapsulate the DIEs
		dwarf::lib::file outdf(fileno(f));
		dwarf::lib::dieset ds(outdf);

		try
		{
			auto p_cu = dynamic_pointer_cast<spec::compile_unit_die>(
				ds.toplevel()->get_first_child());
			assert(p_cu);
			assert(p_cu->get_producer());
			this->m_producer_string = *p_cu->get_producer();
			for (auto i_bt = p_cu->get_first_child();
					i_bt;
					i_bt = i_bt->get_next_sibling())
			{
				// if it's not a named base type, not interested
				if (i_bt->get_tag() != DW_TAG_base_type || !i_bt->get_name()) continue;

				//cerr << "Found a base type!" << endl << **i_bt 
				//	<< ", name " << *((*i_bt)->get_name()) << endl;
				base_types.insert(make_pair(
					base_type(dynamic_pointer_cast<spec::base_type_die>(i_bt)),
					*i_bt->get_name()));
			}
		}
		catch (No_entry) {}
		
		// print(cout, spec::DEFAULT_DWARF_SPEC);
		
		fclose(f);
	}
	
	cxx_compiler::base_type::base_type(shared_ptr<spec::base_type_die> p_d)
		: byte_size(*p_d->get_byte_size()),
		  encoding(p_d->get_encoding()),
		  bit_offset(p_d->get_bit_offset() ? *(p_d->get_bit_offset()) : 0),
		  bit_size (p_d->get_bit_size() 
					? *(p_d->get_bit_size()) 			// stored
					: *p_d->get_byte_size() * 8 - bit_offset // defaulted
					)
	{ 
		/*optional<Dwarf_Unsigned> attr_byte_size = p_d->get_byte_size();
		Dwarf_Half attr_encoding = p_d->get_encoding();
		optional<Dwarf_Unsigned> attr_bit_offset = p_d->get_bit_offset();
		optional<Dwarf_Unsigned> attr_bit_size = p_d->get_bit_size();
		
		Dwarf_Unsigned opt_byte_size = 999UL;
		Dwarf_Unsigned opt_bit_offset = 999UL;
		Dwarf_Unsigned opt_bit_size = 999UL;
		
		if (attr_byte_size) opt_byte_size = *attr_byte_size;
		if (attr_bit_offset) opt_bit_offset = *attr_bit_offset;
		if (attr_bit_size) opt_bit_size = *attr_bit_size;*/
		
		assert(p_d->get_byte_size()); 
	}

	std::ostream& cxx_compiler::print(
		std::ostream& out, 
		const spec::abstract_def& s
	)
	{
		out << "compiler invoked by ";
		for (vector<string>::const_iterator i = compiler_argv.begin();
				i != compiler_argv.end(); i++)
		{
			out << *i << ' ';
		}
		out << endl;
		for (map<cxx_compiler::base_type, string>::const_iterator i = 
				base_types.begin();
				i != base_types.end();
				i++)
		{
			out << i->second << " is " << i->first << endl;
		}
		return out;
	}
	
	std::ostream& operator<<(std::ostream& out, 
		const cxx_compiler::base_type& c)
	{
		out << "<"
			<< "byte_size " << c.byte_size << ", "
			<< "encoding " << spec::DEFAULT_DWARF_SPEC.encoding_lookup(c.encoding) << ", "
			<< "bit offset " << c.bit_offset << ", "
			<< "bit size " << c.bit_size << ">";
		return out;
	}
	
	vector<string> cxx_compiler::parse_cxxflags()
	{
		vector<string> cxxflags_vec;
		const char *cxxflags = getenv("CXXFLAGS");
		if (cxxflags)
		{
			istringstream cxxflags_stream(cxxflags);
			while (cxxflags_stream)
			{
				string s;
				cxxflags_stream >> s;
				if (s.length() > 0) cxxflags_vec.push_back(s);
			}
		}
		return cxxflags_vec;
	}
	
	vector<string>
	cxx_compiler::default_compiler_argv(bool use_cxxflags /* = true */)
	{
		auto argv = vector<string>(1, string("c++"));
		if (use_cxxflags)
		{
			auto cxxflags = parse_cxxflags();
			std::copy(cxxflags.begin(), cxxflags.end(), std::back_inserter(argv));
		}
		return argv;
	}
}}
