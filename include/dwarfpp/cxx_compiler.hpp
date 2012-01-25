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
		class cxx_compiler
		{
			/* Mainly as support for dwarfhpp, this class supports discovering
			 * some DWARF-related properties of a C++ compiler. In particular,
			 * it discovers the range of base type encodings available through
			 * the compiler's implementations of C++ primitive types. */

			std::vector<std::string> compiler_argv;
			
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

		protected:
			std::map<base_type, std::string> base_types;
			string m_producer_string;
			//struct base_type dwarf_base_type(const dwarf::encap::Die_encap_base_type& d);
			//static const std::string dummy_return;
			void discover_base_types();
		public:
			cxx_compiler(const std::vector<std::string>& argv);
			cxx_compiler();
			
			string get_producer_string() const { return m_producer_string; }
			
			std::ostream& print(std::ostream& out, const spec::abstract_def& s);

		};
	}
}

#endif
