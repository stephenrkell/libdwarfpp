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
		using boost::dynamic_pointer_cast;
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
            
            std::string name_for(boost::shared_ptr<spec::type_die> t) 
            { return local_name_for(t); }
            std::vector<std::string> name_parts_for(boost::shared_ptr<spec::type_die> t) 
            { return local_name_parts_for(t); }
            
			static std::string name_from_name_parts(const std::vector<std::string> parts) 
			{
				std::ostringstream s;
				for (auto i_part = parts.begin(); i_part != parts.end(); i_part++)
				{
					if (i_part != parts.begin()) s << "::";
					s << *i_part;
				}
				return s.str();
			}
			
            const std::string local_name_for(boost::shared_ptr<spec::basic_die> p_d,
            	bool use_friendly_names = true) 
            { 
            	return name_from_name_parts(local_name_parts_for(p_d, use_friendly_names));
            }
            std::vector<std::string> local_name_parts_for(boost::shared_ptr<spec::basic_die> p_d,
            	bool use_friendly_names = true) 
            { 
            	if (use_friendly_names && p_d->get_tag() == DW_TAG_base_type)
                {
            	    std::map<base_type, std::string>::iterator found = base_types.find(
                	    base_type(boost::dynamic_pointer_cast<spec::base_type_die>(p_d)));
                    if (found != base_types.end()) return std::vector<std::string>(1, found->second);
                    else assert(false);
	            }
                else
                {
                	return std::vector<std::string>(1, cxx_name_from_die(p_d));
                }
            }
			std::string fq_name_for(boost::shared_ptr<spec::basic_die> p_d)
			{
				return name_from_name_parts(fq_name_parts_for(p_d));
			}
            std::vector<std::string> fq_name_parts_for(boost::shared_ptr<spec::basic_die> p_d)
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
            
			bool cxx_type_can_be_qualified(boost::shared_ptr<spec::type_die> p_d);
			
            std::string cxx_declarator_from_type_die(boost::shared_ptr<spec::type_die> p_d, 
            	boost::optional<const std::string&> infix_typedef_name 
                 = boost::optional<const std::string&>(),
                bool use_friendly_names = true);
			
			bool cxx_assignable_from(boost::shared_ptr<spec::type_die> dest,
				boost::shared_ptr<spec::type_die> source)
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
					if (!boost::dynamic_pointer_cast<spec::pointer_type_die>(dest)->get_type())
					{
						return true; // can always assign to void
					}
					else return 
						boost::dynamic_pointer_cast<spec::pointer_type_die>(source)
							->get_type()
						&& fq_name_for(dynamic_pointer_cast<spec::pointer_type_die>(source)
							->get_type()) 
							== 
							fq_name_for(dynamic_pointer_cast<spec::pointer_type_die>(dest)
							->get_type());
				}
				
				return false;
			}
			
			bool cxx_is_complete_type(boost::shared_ptr<spec::type_die> t)
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
					std::cerr << "DEBUG: testing completeness of cxx type for " << *t << std::endl;
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
        };
        std::ostream& operator<<(std::ostream& s, const cxx_compiler& compil);
    }
}

#endif
