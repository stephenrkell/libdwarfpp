/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * iter.cpp: DIE tree iterators
 *
 * Copyright (c) 2008--17, Stephen Kell.
 */

#include "dwarfpp/abstract.hpp"
#include "dwarfpp/abstract-inl.hpp"
#include "dwarfpp/root.hpp"
#include "dwarfpp/root-inl.hpp"
#include "dwarfpp/iter.hpp"
#include "dwarfpp/iter-inl.hpp"
#include "dwarfpp/dies.hpp"
#include "dwarfpp/dies-inl.hpp"

#include <iostream>
#include <utility>

namespace dwarf
{
	using std::endl;
	using std::dynamic_pointer_cast;
	
	namespace core
	{
		/* printing of iterator_bases -- *avoid* dereferencing, for speed */
		void iterator_base::print(std::ostream& s, unsigned indent_level /* = 0 */) const
		{
			if (!is_real_die_position())
			{
				s << "(no DIE)";
			}
			else
			{
				for (unsigned u = 0; u < indent_level; ++u) s << "\t";
				s << "DIE, offset 0x" << std::hex << offset_here() << std::dec
					<< ", tag " << spec_here().tag_lookup(tag_here());
				if (name_here()) s << ", name \"" << *name_here() << "\""; 
				else s << ", no name";
			}
		}
		void iterator_base::print_with_attrs(std::ostream& s, unsigned indent_level /* = 0 */) const
		{
			if (!is_real_die_position())
			{
				s << "(no DIE)" << endl;
			}
			else
			{
				for (unsigned u = 0; u < indent_level; ++u) s << "\t";
				s << "DIE, offset 0x" << std::hex << offset_here() << std::dec
					<< ", tag " << spec_here().tag_lookup(tag_here())
					<< ", attributes: ";
				//srk31::indenting_ostream is(s);
				//is.inc_level();
				s << endl;
				auto m = copy_attrs();
				m.print(s, indent_level + 1);
				//is << endl << copy_attrs();
				//is.dec_level();
			}
		}
		std::ostream& operator<<(std::ostream& s, const iterator_base& i)
		{
			i.print(s);
			return s;
		}
		iterator_base 
		iterator_base::named_child(const string& name) const
		{
			if (state == WITH_PAYLOAD) 
			{
				/* This means we *can* ask the payload. What will the 
				 * payload do by default? Call the root, of course. 
				 * (But some payloads might be smarter.) */
				auto p_with = dynamic_pointer_cast<with_named_children_die>(cur_payload);
				if (p_with)
				{
					return p_with->named_child(name);
				}
			}
			return p_root->find_named_child(*this, name);
		}
		Dwarf_Off iterator_base::offset_here() const
		{
			if (!is_real_die_position()) { assert(is_root_position()); return 0; }
			return get_handle().get_offset();
		}
		Dwarf_Half iterator_base::tag_here() const
		{
			if (!is_real_die_position()) return 0;
			return get_handle().get_tag();
		}
		//std::unique_ptr<const char, string_deleter>
		opt<string>
		iterator_base::name_here() const
		{
			if (!is_real_die_position()) return opt<string>();
			return get_handle().get_name();
		}
		opt<string>
		iterator_base::global_name_here() const
		{
			auto maybe_name = name_here();
			if (maybe_name &&
					(!has_attr_here(DW_AT_visibility) 
						|| attr(DW_AT_visibility).get_unsigned()
							 != DW_VIS_local)) return maybe_name;
			else return opt<string>();
		}
		bool iterator_base::has_attr_here(Dwarf_Half attr) const
		{
			if (!is_real_die_position()) return false;
			return get_handle().has_attr(attr);
		}		
		iterator_base iterator_base::nearest_enclosing(Dwarf_Half tag) const
		{
			if (tag == DW_TAG_compile_unit)
			{
				Dwarf_Off cu_off = enclosing_cu_offset_here();
				return p_root->cu_pos(cu_off);
			}
			else
			{
				auto cur = *this; // copies!
				while (cur.is_real_die_position() && cur.tag_here() != tag)
				{
					if (!p_root->move_to_parent(cur)) return END;
				}
				if (!cur.is_real_die_position()) return END;
				else return cur;
			}
		}
// 		Dwarf_Off iterator_base::enclosing_cu_offset_here() const
// 		{
// 			return get_handle().enclosing_cu_offset_here();
// 		}
		
		iterator_base iterator_base::parent() const
		{
			return p_root->parent(*this);
		}
		
		const iterator_base iterator_base::END; 
	}
}
