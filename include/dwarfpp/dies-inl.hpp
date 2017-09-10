/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * dies-inl.hpp: inlines declared in dies.hpp.
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_DIES_INL_HPP_
#define DWARFPP_DIES_INL_HPP_

#include <iostream>
#include <utility>
#include <libgen.h> /* FIXME: use a C++-y way to do dirname() */

#include "dies.hpp"

namespace dwarf
{
	using std::string;
	
	namespace core
	{
		/* FIXME: these use libdwarf-specific StringList -- how to abstract this?
		 * Add to abstract_die? Move to root_die? As a private interface on root_die
		 * with which compile_unit_die is friendly? */
		inline std::string compile_unit_die::source_file_name(unsigned o) const
		{
			StringList names(d);
			//if (!names) throw Error(current_dwarf_error, 0);
			/* Source file numbers in DWARF are indexed starting from 1. 
			 * Source file zero means "no source file".
			 * However, our array filesbuf is indexed beginning zero! */
			assert(o <= names.get_len()); // FIXME: how to report error? ("throw No_entry();"?)
			return names[o - 1];
		}

		inline unsigned compile_unit_die::source_file_count() const
		{
			// FIXME: cache some stuff
			StringList names(d);
			return names.get_len();
		}
	}
}

#endif
