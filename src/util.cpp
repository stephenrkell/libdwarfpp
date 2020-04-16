/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * util.cpp: utilities
 *
 * Copyright (c) 2017--, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#include <cstdlib>
#include <sstream>
#include <srk31/endian.hpp>
#include "dwarfpp/util.hpp"

namespace dwarf
{
	namespace core
	{
		std::ofstream null_out;
		unsigned debug_level;
		static void init() __attribute__((constructor));
		static void init()
		{
			const char *env = getenv("DWARFPP_DEBUG_LEVEL");
			if (env)
			{
				std::istringstream s(env);
				s >> debug_level;
			}
		}
	}
	namespace util
	{
		Dwarf_Unsigned read_uleb128(unsigned char const **cur, unsigned char const *limit)
		{
			Dwarf_Unsigned working = 0;
			unsigned char const *start = *cur;
			do
			{
				assert(*cur < limit);
				// the bit offset is 7 * the number of bytes we've already read
				int n7bits = *cur - start;
				// add in the low-order 7 bits
				working |= ((**cur) & ~0x80) << (7 * n7bits);
			} while (*(*cur)++ & 0x80);
			unsigned nbits_read = 7 * (*cur - start);
			assert(nbits_read < 8 * sizeof (Dwarf_Unsigned));
			return working;
		}
		Dwarf_Signed read_sleb128(unsigned char const **cur, unsigned char const *limit)
		{
			unsigned char const *start = *cur;
			Dwarf_Unsigned working = read_uleb128(cur, limit);
			// sign-extend the result...
			unsigned nbits_read = 7 * (*cur - start);
			unsigned top_bits = 8 * (sizeof (Dwarf_Signed)) - nbits_read;
			// ... by shifting up so that we have a 1 in the top position...
			Dwarf_Signed scaled_up = (Dwarf_Signed) (working << top_bits);
			// ... then shifting back down, now as a *signed* number
			return scaled_up >> top_bits;
		}
		uint64_t read_8byte_le(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 8;
			assert(*cur <= limit);
			return (Dwarf_Unsigned) *pos
				| (Dwarf_Unsigned) *(pos + 1) << 8
				| (Dwarf_Unsigned) *(pos + 2) << 16
				| (Dwarf_Unsigned) *(pos + 3) << 24
				| (Dwarf_Unsigned) *(pos + 4) << 32
				| (Dwarf_Unsigned) *(pos + 5) << 40
				| (Dwarf_Unsigned) *(pos + 6) << 48
				| (Dwarf_Unsigned) *(pos + 7) << 56;
		}
		uint32_t read_4byte_le(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 4;
			assert(*cur <= limit);
			return (uint32_t) *pos
				| (uint32_t) *(pos + 1) << 8
				| (uint32_t) *(pos + 2) << 16
				| (uint32_t) *(pos + 3) << 24;
		}
		uint16_t read_2byte_le(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 2;
			assert(*cur <= limit);
			return (uint32_t) *pos
				| (uint32_t)  *(pos + 1) << 8;
		}
		uint64_t read_8byte_be(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 8;
			assert(*cur <= limit);
			return  (Dwarf_Unsigned) *pos       << 56
				  | (Dwarf_Unsigned) *(pos + 1) << 48
				  | (Dwarf_Unsigned) *(pos + 2) << 40
				  | (Dwarf_Unsigned) *(pos + 3) << 32
				  | (Dwarf_Unsigned) *(pos + 4) << 24
				  | (Dwarf_Unsigned) *(pos + 5) << 16
				  | (Dwarf_Unsigned) *(pos + 6) << 8
				  | (Dwarf_Unsigned) *(pos + 7);
		}
		uint32_t read_4byte_be(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 4;
			assert(*cur <= limit);
			return  (Dwarf_Unsigned) *pos       << 24
				  | (Dwarf_Unsigned) *(pos + 1) << 16
				  | (Dwarf_Unsigned) *(pos + 2) << 8
				  | (Dwarf_Unsigned) *(pos + 3);
		}
		uint16_t read_2byte_be(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 2;
			assert(*cur <= limit);
			return (Dwarf_Unsigned) *pos       << 8
				 | (Dwarf_Unsigned) *(pos + 1);
		}
		Dwarf_Addr read_addr(int addrlen, unsigned char const **cur, unsigned char const *limit, bool use_host_byte_order)
		{
			assert(addrlen == 4 || addrlen == 8);
			bool read_be = srk31::host_is_little_endian() ^ use_host_byte_order;
			return static_cast<Dwarf_Addr>(
			       (read_be  && addrlen == 4) ? read_4byte_be(cur, limit)
			     : (read_be  && addrlen == 8) ? read_8byte_be(cur, limit)
			     : (!read_be && addrlen == 4) ? read_4byte_le(cur, limit)
			     : (!read_be && addrlen == 8) ? read_8byte_le(cur, limit)
			     : (assert(false), 0)
			);
		}
	}
}
