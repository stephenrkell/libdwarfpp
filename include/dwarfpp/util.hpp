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
}

#endif
