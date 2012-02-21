#ifndef DWARFPP_CXX_COMPILER_HPP_
#define DWARFPP_CXX_COMPILER_HPP_

#include "lib.hpp"
#include "spec_adt.hpp"

namespace dwarf
{
	namespace tool
	{
		using namespace dwarf::lib;
		using boost::dynamic_pointer_cast;
		using boost::optional;
		using boost::shared_ptr;
		using std::string;
		using std::vector;
		using std::pair;
		using std::multimap;
		using std::endl;
		
		class cxx_compiler
		{
			/* Mainly as support for dwarfhpp, this class supports discovering
			 * some DWARF-related properties of a C++ compiler. In particular,
			 * it discovers the range of base type encodings available through
			 * the compiler's implementations of C++ primitive types. */

			vector<string> compiler_argv;
			
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
				bool operator==(const base_type& arg) const
				{
					return byte_size == arg.byte_size
					&&	encoding == arg.encoding
					&&	bit_offset == arg.bit_offset
					&&	bit_size == arg.bit_size;
				}
				base_type(shared_ptr<spec::base_type_die> p_d);
				friend std::ostream& operator<<(std::ostream& s, const base_type& c);
			}; 

		protected:
			multimap<base_type, string> base_types;
			string m_producer_string;
			//struct base_type dwarf_base_type(const dwarf::encap::Die_encap_base_type& d);
			//static const string dummy_return;
			void discover_base_types();
			static vector<string> parse_cxxflags();
		public:
			cxx_compiler(const vector<string>& argv);
			
			static vector<string>
			default_compiler_argv(bool use_cxxflags = true);
			
			typedef multimap<base_type, string>::iterator base_type_name_iterator;
			
			pair<base_type_name_iterator, base_type_name_iterator> 
			names_for_base_type(const base_type& arg)
			{ return base_types.equal_range(arg); }
			
			cxx_compiler();
			
			string get_producer_string() const { return m_producer_string; }
			
			std::ostream& print(std::ostream& out, const spec::abstract_def& s);

		}; // end class cxx_compiler
		std::ostream& operator<<(std::ostream& s, const cxx_compiler::base_type& c);
	}
}

#endif
