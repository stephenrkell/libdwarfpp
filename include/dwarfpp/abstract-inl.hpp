/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * abstract-inl.hpp: inlines declared in abstract.hpp.
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_ABSTRACT_INL_HPP_
#define DWARFPP_ABSTRACT_INL_HPP_

#include <iostream>
#include <utility>

#include "abstract.hpp"
#include "root.hpp"
#include "iter.hpp"
#include "dies.hpp"

namespace dwarf
{
	using std::string;
	
	namespace core
	{
		inline spec& in_memory_abstract_die::get_spec(root_die& r) const
		{ return r.cu_pos(m_cu_offset).spec_here(); }

		inline basic_die *factory::make_payload(abstract_die&& h, root_die& r)
		{
			Die d(std::move(dynamic_cast<Die&&>(h)));
			if (d.tag_here() == DW_TAG_compile_unit) return make_cu_payload(std::move(d), r);
			else return make_non_cu_payload(std::move(d), r);
		}
	}
}

#endif
