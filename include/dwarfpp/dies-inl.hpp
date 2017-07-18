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
		inline iterator_base
		with_named_children_die::named_child(const std::string& name) const
		{
			/* The default implementation just asks the root. Since we've somehow 
			 * been called via the payload, we have the added inefficiency of 
			 * searching for ourselves first. This shouldn't happen, though 
			 * I'm not sure if we explicitly avoid it. Warn. */
			debug(2) << "Warning: inefficient usage of with_named_children_die::named_child" << endl;

			/* NOTE: the idea about payloads knowing about their children is 
			 * already dodgy because it breaks our "no knowledge of structure" 
			 * property. We can likely work around this by requiring that named_child 
			 * implementations call back to the root if they fail, i.e. that the root
			 * is allowed to know about structure which the child doesn't. 
			 * Or we could call into the root to "validate" our view, somehow, 
			 * to support the case where a given root "hides" some DIEs.
			 * This might be a method
			 * 
			 *  iterator_base 
			 *  root_die::check_named_child(const iterator_base& found, const std::string& name)
			 * 
			 * which calls around into find_named_child if the check fails. 
			 * 
			 * i.e. the root_die has a final say, but the payload itself "hints"
			 * at the likely answer. So the payload can avoid find_named_child's
			 * linear search in the common case, but fall back to it in weird
			 * scenarios (deletions). 
			 */
			root_die& r = get_root();
			Dwarf_Off off = get_offset();
			auto start_iter = r.find(off);
			return r.find_named_child(start_iter, name); 
		}
		
		inline opt<std::string> compile_unit_die::source_file_fq_pathname(unsigned o) const
		{
			string filepath = source_file_name(o);
			opt<string> maybe_dir = this->get_comp_dir();
			if (filepath.length() > 0 && filepath.at(0) == '/') return opt<string>(filepath);
			else if (!maybe_dir) return opt<string>();
			else
			{
				// we want to do 
				// return dir + "/" + path;
				// BUT "path" can contain "../".
				string ourdir = *maybe_dir;
				string ourpath = filepath;
				while (boost::starts_with(ourpath, "../"))
				{
					char *buf = strdup(ourdir.c_str());
					ourdir = dirname(buf); /* modifies buf! */
					free(buf);
					ourpath = ourpath.substr(3);
				}

				return opt<string>(ourdir + "/" + ourpath);
			}
		}

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
