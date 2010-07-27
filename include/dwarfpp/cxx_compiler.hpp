#ifndef DWARFPP_CXX_COMPILER_HPP_
#define DWARFPP_CXX_COMPILER_HPP_

#include "lib.hpp"
#include "spec_adt.hpp"
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <sstream>
#include <boost/regex.hpp>

namespace dwarf
{
	namespace tool
    {
    	using namespace dwarf::lib;
    	class cxx_compiler
        {
            /* Mainly as support for dwarfhpp, this class supports discovering
             * some DWARF-related properties of a C++ compiler. In particular,
             * it discovers the range of base type encodings available through
             * the compiler's implementations of C++ primitive types. */

        	std::vector<std::string> compiler_argv;
            const dwarf::spec::abstract_def *p_spec;
            static const std::vector<std::string> cxx_reserved_words;
            
        public:
            // build a map of base types: 
            // <byte-size, encoding, bit-offset = 0, bit-size = byte-size * 8 - bit-offset>
            struct base_type
            {
    	        Dwarf_Unsigned byte_size;
                Dwarf_Unsigned encoding;
                Dwarf_Unsigned bit_offset;
                Dwarf_Unsigned bit_size;
                bool operator<(const base_type& arg) const
                {
                	return byte_size < arg.byte_size
                    || (byte_size == arg.byte_size && encoding < arg.encoding)
                    || (byte_size == arg.byte_size && encoding == arg.encoding && bit_offset < arg.bit_offset)
                    || (byte_size == arg.byte_size && encoding == arg.encoding && bit_offset == arg.bit_offset && bit_size < arg.bit_size);
                }
                base_type(boost::shared_ptr<spec::base_type_die> p_d);
            };

		private:
            std::map<base_type, std::string> base_types;
    		//struct base_type dwarf_base_type(const dwarf::encap::Die_encap_base_type& d);
            //static const std::string dummy_return;
        	void discover_base_types();
        public:
        	cxx_compiler(const std::vector<std::string>& argv);
            cxx_compiler();
            
            bool is_builtin(boost::shared_ptr<spec::basic_die> p_d) 
		    { bool retval = p_d->get_name() && p_d->get_name()->find("__builtin_") == 0;
              if (retval) std::cerr << 
                	"Warning: DIE at 0x" 
                    << std::hex << p_d->get_offset() << std::dec 
                    << " appears builtin and will be ignored." << std::endl;
              return retval; 
            }
            
            const std::string name_for(boost::shared_ptr<spec::type_die> t) 
            { return local_name_for(t); }
            
            const std::string local_name_for(boost::shared_ptr<spec::basic_die> p_d) { 
            	if (p_d->get_tag() == DW_TAG_base_type)
                {
            	    std::map<base_type, std::string>::iterator found = base_types.find(
                	    base_type(boost::dynamic_pointer_cast<spec::base_type_die>(p_d)));
                    if (found != base_types.end()) return found->second;
                    else assert(false);
	            }
                else
                {
                	return cxx_name_from_die(p_d);
                }
            }

            const std::string fq_name_for(boost::shared_ptr<spec::basic_die> p_d)
            {
            	if (p_d->get_offset() != 0UL && p_d->get_parent()->get_tag() != DW_TAG_compile_unit)
                {
                	return fq_name_for(p_d->get_parent()) + "::" + cxx_name_from_die(p_d);
				}
                else return cxx_name_from_die(p_d);
            }
            
        	friend std::ostream& operator<<(std::ostream& s, const cxx_compiler& compil);
            bool is_reserved(const std::string& word)
            {
            	return std::find(cxx_reserved_words.begin(),
                	cxx_reserved_words.end(), 
                    word) != cxx_reserved_words.end();
            }
            bool is_valid_cxx_ident(const std::string& word)
            {
            	static const boost::regex e("[a-zA-Z_][a-zA-Z0-9_]*");
            	return !is_reserved(word) &&
                	regex_match(word, e);	
            }
            std::string make_valid_cxx_ident(const std::string& word)
            {
            	// FIXME: make this robust to illegal characters other than spaces
            	std::string working = word;
            	return is_valid_cxx_ident(word) ? word
                	: (std::replace(working.begin(), working.end()-1, ' ', '_'), working);
            }

            bool type_infixes_name(boost::shared_ptr<spec::basic_die> p_d);

			std::string cxx_name_from_string(const std::string& s, const char *prefix);
            std::string cxx_name_from_die(boost::shared_ptr<spec::basic_die> p_d);
            
            std::string cxx_declarator_from_type_die(boost::shared_ptr<spec::type_die> p_d, 
            	boost::optional<const std::string&> infix_typedef_name 
                 = boost::optional<const std::string&>());
        };
        std::ostream& operator<<(std::ostream& s, const cxx_compiler& compil);
    }
}

#endif
