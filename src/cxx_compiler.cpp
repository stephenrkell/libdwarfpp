#include "cxx_compiler.hpp"

#include <cstdlib>
#include <sstream>
#include <fstream>
#include <string>
#include <cstring>
#include <map>

#include "adt.hpp"

using boost::dynamic_pointer_cast;

namespace dwarf { namespace tool {
	cxx_compiler::cxx_compiler() : compiler_argv(1, std::string("c++")) 
	{
		discover_base_types();
	}
	cxx_compiler::cxx_compiler(const std::vector<std::string>& argv)
		: 	compiler_argv(argv)
	{
		discover_base_types();
	}
	
	void cxx_compiler::discover_base_types()
	{
		/* Discover the DWARF descriptions of our compiler's base types.
		 * - Output and compile a test program generating all the base types
		 * we know of.
		 * - Compile it to a temporary file, read the DWARF and build a map
		 * of descriptions to compiler-builtin-typenames.
		 * - FIXME: it'd be nice if we could account for compiler flags etc.. */

		std::ostringstream test_src;
		const char *base_typenames[] = {
			"char",
			"unsigned char",
			"short",
			"unsigned short",
			"int",
			"unsigned int",
			"long",
			"unsigned long",
			"long long",
			"unsigned long long",
			"float",
			"double",
			"long double",
			"bool",		
			"wchar_t",
			NULL
		};

		std::vector<std::string> base_typenames_vec(&base_typenames[0],
			&base_typenames[sizeof base_typenames / sizeof (const char *) - 1]);

		int funcount = 0;
		for (std::vector<std::string>::iterator i_tn = base_typenames_vec.begin();
				i_tn != base_typenames_vec.end(); ++i_tn)
		{
			// prototype
			test_src << "void foo_" << funcount++
				<< "(" << *i_tn << " arg);";
			// definition
			test_src << "void foo_" << funcount++
				<< "(" << *i_tn << " arg) {};" << std::endl;
		}

		const char template_string[] = "/tmp/tmp.XXXXXX";
		char tmpnam_src_outfile_buf[sizeof template_string];
		strcpy(tmpnam_src_outfile_buf, template_string);

		int mkstemp_return = mkstemp(tmpnam_src_outfile_buf);
		assert(mkstemp_return != -1);
		std::ofstream test_src_outfile(tmpnam_src_outfile_buf);

		const std::string tmp_s = test_src.str();
		test_src_outfile << tmp_s.c_str();
		test_src_outfile.close();

		std::ostringstream cmdstream;
		char tmpnam_cxxoutput_buf[sizeof template_string];
		strcpy(tmpnam_cxxoutput_buf, template_string);
		
		mkstemp_return = mkstemp(tmpnam_cxxoutput_buf);
		assert(mkstemp_return != -1);
		
		// add compiler command+args to cmd
		std::vector<std::string> cmd = compiler_argv;
		cmd.push_back("-g"); 
 		cmd.push_back("-c"); 
		cmd.push_back("-o");
		cmd.push_back(std::string(tmpnam_cxxoutput_buf));
		cmd.push_back("-x c++");
		cmd.push_back(std::string(tmpnam_src_outfile_buf));
		
		for (std::vector<std::string>::iterator i_arg = cmd.begin();
				i_arg != cmd.end(); ++i_arg) cmdstream << *i_arg << ' ';
		
		std::cerr << "About to execute: " << cmdstream.str().c_str() << std::endl;
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
			auto p_cu = ds.toplevel()->get_first_child();
			assert(p_cu->get_tag() == DW_TAG_compile_unit);
			for (auto i_bt = p_cu->get_first_child();
					i_bt;
					i_bt = i_bt->get_next_sibling())
			{
				// if it's not a named base type, not interested
				if (i_bt->get_tag() != DW_TAG_base_type || !i_bt->get_name()) continue;

				//std::cerr << "Found a base type!" << std::endl << **i_bt 
				//	<< ", name " << *((*i_bt)->get_name()) << std::endl;
				base_types.insert(std::make_pair(
					base_type(dynamic_pointer_cast<spec::base_type_die>(i_bt)),
					*i_bt->get_name()));
			}
		}
		catch (No_entry) {}
		
		// print(std::cout, spec::DEFAULT_DWARF_SPEC);
		
		fclose(f);
	}
	
	cxx_compiler::base_type::base_type(boost::shared_ptr<spec::base_type_die> p_d)
		: byte_size(*p_d->get_byte_size()),
		  encoding(p_d->get_encoding()),
		  bit_offset(p_d->get_bit_offset() ? *(p_d->get_bit_offset()) : 0),
		  bit_size (p_d->get_bit_size() 
					? *(p_d->get_bit_size()) 			// stored
					: *p_d->get_byte_size() * 8 - bit_offset // defaulted
					)
	{ 
		/*boost::optional<Dwarf_Unsigned> attr_byte_size = p_d->get_byte_size();
		Dwarf_Half attr_encoding = p_d->get_encoding();
		boost::optional<Dwarf_Unsigned> attr_bit_offset = p_d->get_bit_offset();
		boost::optional<Dwarf_Unsigned> attr_bit_size = p_d->get_bit_size();
		
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
		for (std::vector<std::string>::const_iterator i = compiler_argv.begin();
				i != compiler_argv.end(); i++)
		{
			out << *i << ' ';
		}
		out << std::endl;
		for (std::map<cxx_compiler::base_type, std::string>::const_iterator i = 
				base_types.begin();
				i != base_types.end();
				i++)
		{
			out << i->second << " is <"
				<< "byte_size " << i->first.byte_size << ", "
				<< "encoding " << s.encoding_lookup(i->first.encoding) << ", "
				<< "bit offset " << i->first.bit_offset << ", "
				<< "bit size " << i->first.bit_size << ">" << std::endl;
		}
		return out;
	}
}}
