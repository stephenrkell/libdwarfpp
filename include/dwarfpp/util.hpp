/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * util.hpp: utilities
 *
 * Copyright (c) 2017--, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_UTIL_HPP_
#define DWARFPP_UTIL_HPP_

#include <fstream>
#include <iostream>
#include <libdwarf.hpp>

namespace dwarf
{
	namespace core
	{
		extern std::ofstream null_out;
		extern unsigned debug_level;
		inline std::ostream& debug(unsigned level = 1)
		{
			if (debug_level >= level) return std::cerr;
			else return null_out;
		}
		#define debug_expensive(lvl, args...) \
			((debug_level >= (lvl)) ? (debug(lvl) args) : (debug(lvl)))
	}
	namespace util
	{
		Dwarf_Unsigned read_uleb128(unsigned char const **cur, unsigned char const *limit);
		Dwarf_Signed read_sleb128(unsigned char const **cur, unsigned char const *limit);
		uint64_t read_8byte_le(unsigned char const **cur, unsigned char const *limit);
		uint32_t read_4byte_le(unsigned char const **cur, unsigned char const *limit);
		uint16_t read_2byte_le(unsigned char const **cur, unsigned char const *limit);
		uint64_t read_8byte_be(unsigned char const **cur, unsigned char const *limit);
		uint32_t read_4byte_be(unsigned char const **cur, unsigned char const *limit);
		uint16_t read_2byte_be(unsigned char const **cur, unsigned char const *limit);
		Dwarf_Addr read_addr(int addrlen, unsigned char const **cur, unsigned char const *limit,
			bool use_host_byte_order);
	}
}

#endif
