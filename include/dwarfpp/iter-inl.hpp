/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * iter-inl.hpp: inlines declared in iter.hpp.
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_ITER_INL_HPP_
#define DWARFPP_ITER_INL_HPP_

#include <iostream>
#include <utility>

#include "iter.hpp"
#include "dies.hpp"

namespace dwarf
{
	using std::string;
	using std::endl;
	
	namespace core
	{
		inline spec& iterator_base::spec_here() const
		{
			if (tag_here() == DW_TAG_compile_unit)
			{
				// we only ask CUs for their spec after payload construction
				assert(state == WITH_PAYLOAD);
				auto p_cu = dynamic_pointer_cast<compile_unit_die>(cur_payload);
				assert(p_cu);
				switch(p_cu->version_stamp)
				{
					case 2: return ::dwarf::spec::dwarf_current; // HACK: we don't model old DWARFs for now
					case 4: return ::dwarf::spec::dwarf_current;
					default: 
						debug() << "Warning: saw unexpected DWARF version stamp " 
							<< p_cu->version_stamp << endl;
						return ::dwarf::spec::dwarf_current;
				}
			}
			else return get_handle().get_spec(*p_root);
		}
		
		inline iterator_df<compile_unit_die>
		iterator_base::enclosing_cu() const
		{ return get_root().pos(enclosing_cu_offset_here(), 1, opt<Dwarf_Off>(0UL)); }
		inline sequence< iterator_sibs<> >
		iterator_base::children_here()
		{
			return std::make_pair<iterator_sibs<>, iterator_sibs<> >(
				p_root->first_child(*this), 
				iterator_base::END
			);
		}
		inline sequence< iterator_sibs<> >
		iterator_base::children_here() const
		{ return const_cast<iterator_base*>(this)->children(); }
		// synonyms
		inline sequence< iterator_sibs<> >
		iterator_base::children() { return children_here(); }
		inline sequence< iterator_sibs<> >
		iterator_base::children() const { return children_here(); }
	}
}

#endif
