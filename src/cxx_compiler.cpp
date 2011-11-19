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
    		    i_tn != base_typenames_vec.end(); i_tn++)
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
        		i_arg != cmd.end(); i_arg++) cmdstream << *i_arg << ' ';
        
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
        
        p_spec = &(ds.get_spec());
        
        //std::cout << *this;
        
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

    bool cxx_compiler::type_infixes_name(boost::shared_ptr<spec::basic_die> p_d)
    {
        return 
            p_d->get_tag() == DW_TAG_subroutine_type
            ||  p_d->get_tag() == DW_TAG_array_type
            || 
            (p_d->get_tag() == DW_TAG_pointer_type &&
                dynamic_pointer_cast<spec::pointer_type_die>(p_d)->get_type()
                && 
                dynamic_pointer_cast<spec::pointer_type_die>(p_d)->get_type()
                    ->get_tag()	== DW_TAG_subroutine_type);
	}            

	std::string cxx_compiler::cxx_name_from_string(const std::string& s, const char *prefix)
    {
    	if (is_reserved(s)) 
        {
            std::cerr << "Warning: generated C++ name `" << (prefix + s) 
                << " from reserved word " << s << std::endl;
            return prefix + s; // being reserved implies s is lexically okay
        }
        else if (is_valid_cxx_ident(s)) return s;
        else // something is lexically illegal about s
        {
            return make_valid_cxx_ident(s);
        }
    }
    std::string cxx_compiler::cxx_name_from_die(boost::shared_ptr<spec::basic_die> p_d)
    {
        if (p_d->get_name()) return cxx_name_from_string(*p_d->get_name(), "_dwarfhpp_");
        else 
        {
            std::ostringstream s;
            s << "_dwarfhpp_anon_" << std::hex << p_d->get_offset();
            return s.str();
        }
	}

    std::string cxx_compiler::cxx_declarator_from_type_die(boost::shared_ptr<spec::type_die> p_d, 
        boost::optional<const std::string&> infix_typedef_name,
        bool use_friendly_names /*= true*/ 
         /*= boost::optional<const std::string&>()*/)
    {
/* Following is an aborted attempt to avoid typecasing. */
//     	struct can_emit_decl {
//         	virtual std::string emit_declarator(boost::optional<const std::string&> infix_name) = 0;
//         };
//         template <typename Rep, Dwarf_Half Tag> struct decl_emitter : public can_emit_decl
//         {
//         	// constructor
//             typename abstract::tag<Rep, Tag>::type& die m_die;
//             decl_emitter(typename abstract::tag<Rep, Tag>::type& die) : m_die(die) {}
//             
//             // can_emit_decl is left pure
//         }
//         template <typename Rep> struct decl_emitter<Rep, DW_TAG_base_type> : public can_emit_decl
//         {
//         	void 
//         }

	    std::string name_prefix;
	    switch (p_d->get_tag())
        {
    	    // return the friendly compiler-determined name or not, depending on argument
    	    case DW_TAG_base_type:
	            return local_name_for(dynamic_pointer_cast<spec::base_type_die>(p_d),
                	use_friendly_names);
            case DW_TAG_typedef:
        	    return *p_d->get_name();
            case DW_TAG_pointer_type: {
        	    boost::shared_ptr<spec::pointer_type_die> pointer 
                 = dynamic_pointer_cast<spec::pointer_type_die>(p_d);
                if (pointer->get_type())
                {
            	    if (pointer->get_type()->get_tag() == DW_TAG_subroutine_type)
                    {
                	    // we have a pointer to a subroutine type -- pass on the infix name
                	    return cxx_declarator_from_type_die(
                            pointer->get_type(), infix_typedef_name);
                    }
                    else return cxx_declarator_from_type_die(pointer->get_type()) + "*";
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
        	    boost::shared_ptr<spec::array_type_die> arr
                 = dynamic_pointer_cast<spec::array_type_die>(p_d);
			    // calculate array size, if we have a subrange type
                auto array_size = arr->element_count();
                std::ostringstream arrsize; 
                if (array_size) arrsize << *array_size;
        	    return cxx_declarator_from_type_die(arr->get_type())
                    + " " + (infix_typedef_name ? *infix_typedef_name : "") + "[" 
                    // add size, if we have a subrange type
                    + arrsize.str()
                    + "]";
            }
            case DW_TAG_subroutine_type: {
        	    std::ostringstream s;
                boost::shared_ptr<spec::subroutine_type_die> subroutine_type 
                 = dynamic_pointer_cast<spec::subroutine_type_die>(p_d);
        	    s << (subroutine_type->get_type() 
                    ? cxx_declarator_from_type_die(
                        subroutine_type->get_type()) 
                    : std::string("void "));
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
		                         dynamic_pointer_cast<spec::formal_parameter_die>(i)->get_type());
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
            case DW_TAG_const_type: {
				/* Note that many DWARF emitters record C/C++ "const void" (as in "const void *")
				 * as a const type with no "type" attribute. So handle this case. */
        	    auto chained_type =  dynamic_pointer_cast<spec::const_type_die>(p_d)->get_type();
				return "const " + (chained_type ? cxx_declarator_from_type_die(chained_type) : " void ");
				}
            case DW_TAG_volatile_type: {
				/* Ditto as for const_type. */
        	    auto chained_type =  dynamic_pointer_cast<spec::volatile_type_die>(p_d)->get_type();
				return "volatile " + (chained_type ? cxx_declarator_from_type_die(chained_type) : " void ");
				}
            case DW_TAG_structure_type:
        	    name_prefix = "struct ";
                goto handle_named_type;
            case DW_TAG_union_type:
        	    name_prefix = "union ";
                goto handle_named_type;
            case DW_TAG_class_type:
        	    name_prefix = "class ";
                goto handle_named_type;
            handle_named_type:
		    default:
				return name_prefix + cxx_name_from_die(p_d);
        }
    }

    std::ostream& operator<<(std::ostream& s, const cxx_compiler& compil)
    {
    	s << "compiler invoked by ";
        for (std::vector<std::string>::const_iterator i = compil.compiler_argv.begin();
        		i != compil.compiler_argv.end(); i++)
        {
            s << *i << ' ';
        }
        s << std::endl;
    	for (std::map<cxx_compiler::base_type, std::string>::const_iterator i = 
        		compil.base_types.begin();
                i != compil.base_types.end();
                i++)
        {
            s << i->second << " is <"
            	<< "byte_size " << i->first.byte_size << ", "
                << "encoding " << compil.p_spec->encoding_lookup(i->first.encoding) << ", "
                << "bit offset " << i->first.bit_offset << ", "
                << "bit size " << i->first.bit_size << ">" << std::endl;
		}
        return s;
    }
    
    //const std::string cxx_compiler::dummy_return = "BAD_TYPE";
    //const char *cxx_compiler::cxx_reserved_words[] = {
    const std::vector<std::string> cxx_compiler::cxx_reserved_words = {
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
}}
