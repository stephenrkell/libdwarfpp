/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * util.cpp: utilities
 *
 * Copyright (c) 2017--, Stephen Kell.
 */

#include <cstdlib>
#include <sstream>
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
}
