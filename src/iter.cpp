/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * iter.cpp: DIE tree iterators
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
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
		bool iterator_base::eq_by_type_equality::operator()(const iterator_base& i1,
			const iterator_base& i2) const
		{
			if (!(i1.is_a<type_die>() && i2.is_a<type_die>())) return i1 == i2;
			return dwarf::core::type_eq_fn(i1.as_a<type_die>(), i2.as_a<type_die>());
		}

		/* This is trickier than it looks. Easy to break transitivity of '<'.
		 * if we have   p1   p2   p3
		 * where by <,  p1 < p2 < p3      if '<' is just using the offset comparison
		 * and so       p1 <      p3
		 * ... if it happens that p1 == p3  (type DIEs) but p2 is not a type DIE
		 * we have a problem because it remains that p1<p2 and p2<p3. So we
		 * have to define a better operator<. */
		/* FIXME: One problem with this, and with our caching of equality, is that
		 * if DIEs are mutable, then the ordering on their iterators changes
		 * as they are mutated, e.g as equality is broken by a particular mutation.
		 */
		bool iterator_base::less_by_type_equality::operator()(const iterator_base& p1,
			const iterator_base& p2) const
		{
			/* END has an infinite offset. */
			if (p1.is_end_position() && p2.is_end_position()) return false; // equal
			if (p1.is_end_position() && !p2.is_end_position()) return false; // p1 is >
			if (!p1.is_end_position() && p2.is_end_position()) return true; // p1 is <

			// type DIEs come before other DIEs
			if (!(p1.is_a<type_die>() && p2.is_a<type_die>()))
			{
				/* They're not both type DIEs. Is either a type DIE? */
				if (p1.is_a<type_die>())
				{
					// p2 is not, so p1 is less
					return true;
				}
				if (p2.is_a<type_die>())
				{
					// p1 is not, so p2 is less
					return false;
				}
				// else neither of them is, so just compare by offset
				return p1.offset_here() < p2.offset_here();
			}
			bool ret;
			// if they are type DIEs, are they equal?
			/* If we get here, we have two unequal type DIEs. We have to compare them
			 * in a way that respect transitivity, which their offset *doesn't*. */
			root_die& p1_r = p1.root();
			root_die& p2_r = p2.root();
			map<Dwarf_Off, list<set<Dwarf_Off>>::iterator>::iterator p1_eq_found;
			map<Dwarf_Off, list<set<Dwarf_Off>>::iterator>::iterator p2_eq_found;
			if (dwarf::core::type_eq_fn(p1.as_a<type_die>(), p2.as_a<type_die>()))
			{ ret = false; goto out; }
			if ((uintptr_t) &p1_r < (uintptr_t) &p2_r) { ret = true; goto out; }
			if ((uintptr_t) &p1_r > (uintptr_t) &p2_r) { ret = false; goto out; }
			p1_eq_found = p1_r.equivalence_class_of.find(p1.offset_here());
			assert(p1_eq_found != p1_r.equivalence_class_of.end());
			p2_eq_found = p2_r.equivalence_class_of.find(p2.offset_here());
			assert(p2_eq_found != p1_r.equivalence_class_of.end());
			assert(p1_eq_found != p2_eq_found);
			/* Now we have to compare based on equivalence class, which
			 * is represented by a list iterator.
			 * We want to compare the list iterators using '<',
			 * but that is not defined on a list iterator.
			 * Instead, search forwards from p2_eq and see if we find
			 * p1_eq. If we don't, p1 is less. */
			for (auto i = p2_eq_found->second; i != p1_r.equivalence_classes.end(); ++i)
			{
				if (i == p1_eq_found->second) { ret = false; goto out; }
			}
			ret = true;
		out:
			return ret;
		}
	}
}
