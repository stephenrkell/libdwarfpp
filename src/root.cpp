/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * root.cpp: root node of DIE tree
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#include "dwarfpp/root.hpp"
#include "dwarfpp/root-inl.hpp"
#include "dwarfpp/iter.hpp"
#include "dwarfpp/iter-inl.hpp"
#include "dwarfpp/frame.hpp"

#include <iostream>
#include <srk31/indenting_ostream.hpp>
#include <srk31/algorithm.hpp>

namespace dwarf
{
	using std::endl;
	namespace core
	{
		// out-of-line these constructors here while we're debugging them
		/* Dummy constructor used only for dummy DIEs. */
		basic_die::basic_die(spec& s) : refcount(0), d(nullptr, 0)
		{}
		/* Constructor used for in-memory. DIEs How can we tell them apart? See is_dummy(). */
		basic_die::basic_die(spec& s, root_die& r) : refcount(0), d(nullptr, 0)
		{
			/* This is now done in the constructors for each in-memory DIE class,
			 * because we can't get the offset from this context. */
			//r.live_dies.insert(make_pair(get_offset(), this));
		}
		
		/* printing of basic_dies */
		void basic_die::print(std::ostream& s) const
		{
			s << "DIE, offset 0x" << std::hex << get_offset() << std::dec
				<< ", tag " << find_self().spec_here().tag_lookup(get_tag());
			if (get_name()) s << ", name \"" << *get_name() << "\""; 
			else s << ", no name";
		}
		void basic_die::print_with_attrs(std::ostream& s) const
		{
			s << "DIE, offset 0x" << std::hex << get_offset() << std::dec
				<< ", tag " << find_self().spec_here().tag_lookup(get_tag())
				<< ", attributes: ";
			srk31::indenting_ostream is(s);
			is.inc_level();
			is << endl << copy_attrs();
			is.dec_level();
		}
		/* print a single DIE */
		std::ostream& operator<<(std::ostream& s, const basic_die& d)
		{
			d.print(s);
			return s;
		}
		/* print a whole tree of DIEs -- only defined on iterators. 
		 * If we made it a method on iterator_base, it would have
		 * to copy itself. f we make it a method on root_die, it does
		 * not have to copy. */
		void root_die::print_tree(iterator_base&& begin, std::ostream& s) const
		{
			unsigned start_depth = begin.depth();
			Dwarf_Off start_offset = begin.offset_here();
			for (iterator_df<> i = std::move(begin);
				(i.is_root_position() || i.is_real_die_position()) 
					&& (i.offset_here() == start_offset || i.depth() > start_depth); 
				++i)
			{
				// for (unsigned u = 0u; u < i.depth() - start_depth; ++u) s << '\t';
				//i->print_with_attrs(s, const_cast<root_die&>(*this));
				i.print_with_attrs(s, i.depth() - start_depth);
			}
		}

		std::ostream& operator<<(std::ostream& s, const root_die& r)
		{
			r.print_tree(r.begin(), s);
			return s;
		}

		/* Individual attribute access within basic_die */
		encap::attribute_map  basic_die::all_attrs() const
		{
			return copy_attrs();
		}
		encap::attribute_value basic_die::attr(Dwarf_Half a) const
		{
			Attribute attr(d, a);
			return encap::attribute_value(attr, d, get_root());
		}
		void basic_die::left_merge_attrs(encap::attribute_map& m, const encap::attribute_map& arg)
		{
			for (auto i_attr = arg.begin(); i_attr != arg.end(); ++i_attr)
			{
				auto found = m.find(i_attr->first);
				if (found == m.end()) m.insert(make_pair(i_attr->first, i_attr->second));
			}
		}
		/* The same, but seeing through DW_AT_abstract_origin and DW_AT_specification references. */
		encap::attribute_map basic_die::find_all_attrs() const
		{
			encap::attribute_map m = copy_attrs();
			// merge with attributes of abstract_origin and specification
			if (has_attr(DW_AT_abstract_origin))
			{
				encap::attribute_map origin_m = attr(DW_AT_abstract_origin).get_refiter()->all_attrs();
				left_merge_attrs(m, origin_m);
			}
			else if (has_attr(DW_AT_specification))
			{
				encap::attribute_map origin_m = attr(DW_AT_specification).get_refiter()->all_attrs();
				left_merge_attrs(m, origin_m);
			}
			else if (has_attr(DW_AT_declaration))
			{
				/* How do we get to the "real" DIE from this specification? The 
				 * specification attr doesn't tell us, so we have to search. */
				iterator_df<> found = find_definition();
				if (found) left_merge_attrs(m, found->all_attrs());
			}
			return m;
		}
		encap::attribute_value basic_die::find_attr(Dwarf_Half a) const
		{
			if (has_attr(a)) { return attr(a); }
			else if (has_attr(DW_AT_abstract_origin))
			{
				return attr(DW_AT_abstract_origin).get_refiter()->find_attr(a);
			}
			else if (has_attr(DW_AT_specification))
			{
				/* For the purposes of this algorithm, if a debugging information entry S has a
				   DW_AT_specification attribute that refers to another entry D (which has a 
				   DW_AT_declaration attribute), then S inherits the attributes and children of D, 
				   and S is processed as if those attributes and children were present in the 
				   entry S. Exception: if a particular attribute is found in both S and D, the 
				   attribute in S is used and the corresponding one in D is ignored.
				 */
				// FIXME: handle children similarly!

				// NOTE: we don't find_attr because I don't think chains of s->d->d->d-> 
				// are allowed.

				auto decl = attr(DW_AT_specification).get_refiter();
				if (decl.has_attr(a)) return decl->attr(a);
			}
			else if (has_attr(DW_AT_declaration))
			{
				/* How do we get to the "real" DIE from this declaration? The 
				 * declaration attr doesn't tell us, so we have to search.. */
				iterator_df<> found = find_definition();
				if (found && found.offset_here() != get_offset()) return found->find_attr(a);
			}
			return encap::attribute_value(); // a.k.a. a NO_ATTR-valued attribute_value
		}
		iterator_base basic_die::find_definition() const
		{
			/* For most DIEs, we just return ourselves if we don't have DW_AT_specification. */
			return find_self();
// 			if (has_attr(DW_AT_specification))
// 			{
// 				/* This implies we're supposed to be a definition, although
// 				 * we can rely on details on the referenced specification attribute. */
// 				return iterator_base::END;
// 			} else return find_self();
		}
		
		root_die::root_die(int fd)
		 :  dbg(fd), 
			visible_named_grandchildren_is_complete(false),
			p_fs(new FrameSection(get_dbg(), true)), 
			current_cu_offset(0UL), returned_elf(nullptr), 
			first_cu_offset(),
			last_seen_cu_header_length(),
			last_seen_version_stamp(),
			last_seen_abbrev_offset(),
			last_seen_address_size(),
			last_seen_offset_size(),
			last_seen_extension_size(),
			last_seen_next_cu_header()
		{ assert(p_fs != 0); }
		
		root_die::~root_die() { delete p_fs; }
		
		::Elf *root_die::get_elf()
		{
			if (returned_elf) return returned_elf;
			else 
			{
				int ret = dwarf_get_elf(dbg.raw_handle(), reinterpret_cast<Elf_opaque_in_libdwarf **>(&returned_elf), &core::current_dwarf_error);
				if (ret == DW_DLV_ERROR) return nullptr;
				else 
				{
					assert(ret == DW_DLV_OK);
					return returned_elf;
				}
			}
		}
		
		/* Moving around, there are a few concerns to deal with. 
		 * 1. maintaining the parent cache
		 * 2. exploiting the parent cache
		 * 3. libdwarf's compilation unit quirk 
		 * 4. maintaining the depth field
		 * 5. maintaining the sticky set
		 * 6. exploiting the sticky set.
		 *
		 * We deal with the first four here. 
		 * Sticky set exploitation is done in iterator_base::iterator_base(handle).
		 * Sticky set maintenance is done in iterator_base's copy constructor 
		 * ... and root_die::make_payload. */
		iterator_base 
		root_die::parent(const iterator_base& it)
		{
			assert(&it.get_root() == this);
			if (it.tag_here() == DW_TAG_compile_unit) 
			{
				assert(it.get_depth() == 1);
				return it.get_root().begin();
			}
			else if (it.offset_here() == 0UL) return iterator_base::END;
			else
			{
				assert(it.get_depth() > 0);
				auto found = parent_of.find(it.offset_here());
				if (found == parent_of.end()) 
				{
					// find ourselves downwards, then try again
					debug(2) << "Warning: searching for parent of " << it << " all the way from root." << endl;
					auto found_again = find_downwards(it.offset_here());
					found = parent_of.find(it.offset_here());
				}
				assert(found != parent_of.end());
				assert(found->first == it.offset_here());
				assert(found->second < it.offset_here());
				//debug(2) << "Parent cache says parent of 0x" << std::hex << found->first
				// << " is 0x" << std::hex << found->second << std::dec << endl;
				
				// this is using the "dieoff" constructor
				// auto maybe_handle = Die::try_construct(*this, found->second);
				//if (maybe_handle)
				//{
				//	// update "it" with its parent
				//	iterator_base new_it(Die(std::move(maybe_handle)), it.get_depth() - 1, *this);
				//	assert(new_it.offset_here() == found->second);
				//	return new_it;
				//}
				//else
				//{
				//	// no such DIE?!
				//	assert(false);
				//}
				// just use pos()
				return pos(found->second, it.depth() - 1, opt<Dwarf_Off>());
			}
		}
		
		bool 
		root_die::move_to_parent(iterator_base& it)
		{
			auto maybe_parent = parent(it); 
			if (maybe_parent != iterator_base::END) 
			{
				/* check we really got the parent! */
				assert(parent_of.find(it.offset_here()) != parent_of.end());
				assert(maybe_parent.offset_here() == parent_of[it.offset_here()]);
				it = std::move(maybe_parent); 
				return true; 
			}
			else return false;
		}
		
		/* These move_to functions are great for implementing ++ and -- on iterators. 
		 * But sometimes we want to spawn one iterator from another, e.g. to iterate
		 * over children of an interesting DIE encountered during an enclosing iteration.
		 * So our root_die needs the ability to create fresh libdwarf handles without 
		 * overwriting the one we started with. */
		
		iterator_base
		root_die::first_child(const iterator_base& it)
		{
			assert(it.is_real_die_position() || it.is_root_position());
			assert(&it.get_root() == this);
			Dwarf_Off start_offset = it.offset_here();
			Die::handle_type maybe_handle(nullptr, Die::deleter(nullptr)); // TODO: reenable deleter's default constructor
			
			// check for cached edges 
			auto found = first_child_of.find(start_offset);
			if (found != first_child_of.end())
			{
				auto found_live = live_dies.find(found->second);
				if (found_live != live_dies.end())
				{
					return iterator_base(static_cast<abstract_die&&>(*found_live->second),
						it.maybe_depth() ? it.depth() + 1 : opt<unsigned short>(),
						*this);
				} // else fall through
			}
			
			// populate maybe_handle with the first child DIE's handle
			if (start_offset == 0UL) 
			{
				// do the CU thing
				bool ret1 = clear_cu_context();
				assert(ret1);
				bool ret2 = advance_cu_context(); 
				if (!ret2)
				{
					/* We don't have any CUs *in the dwarf file*. 
					 * And if we had one in memory, we'd have found it earlier. */
					return iterator_base::END;
				}
				maybe_handle = std::move(Die::try_construct(*this));
				
				if (maybe_handle) first_child_of[0UL] = current_cu_offset;
			}
			else
			{
				// do the non-CU thing
				// FIXME: in-memory case
				maybe_handle = std::move(Die::try_construct(it));
			}
			// shared parent cache logic
			if (maybe_handle)
			{
				iterator_base new_it(Die(std::move(maybe_handle)),
					it.maybe_depth() ? it.depth() + 1 : opt<unsigned short>(),
					it.get_root());
				// install in parent cache, first_child_of
				parent_of[new_it.offset_here()] = start_offset;
				first_child_of[start_offset] = new_it.offset_here();
				return new_it;
			} else return iterator_base::END;
		}
		

		bool 
		root_die::move_to_first_child(iterator_base& it)
		{
			unsigned start_depth = it.get_depth();
			auto maybe_child = first_child(it); 
			if (maybe_child != iterator_base::END) 
			{ it = std::move(maybe_child); assert(it.depth() == start_depth + 1); return true; }
			else return false;
		}
		iterator_base
		root_die::find_named_child(const iterator_base& start, const string& name)
		{
			auto children = start.children_here();
			for (auto i_child = std::move(children.first); i_child != children.second; ++i_child)
			{
				if (i_child.name_here() && *i_child.name_here() == name)
				{
					return std::move(i_child);
				}
			}
			return iterator_base::END;
		}
		
		iterator_base
		root_die::find_visible_grandchild_named(const string& name)
		{
			vector<string> name_vec(1, name);
			std::vector<iterator_base > found;
			resolve_all_visible_from_root(name_vec.begin(), name_vec.end(), 
				found, 1);
			return found.size() > 0 ? *found.begin() : iterator_base::END;
		}
		
		std::vector<iterator_base>
		root_die::find_all_visible_grandchildren_named(const string& name)
		{
			vector<string> name_vec(1, name);
			std::vector<iterator_base > found;
			resolve_all_visible_from_root(name_vec.begin(), name_vec.end(), 
				found, 0);
			return found;
		}
		
		bool root_die::is_under(const iterator_base& i1, const iterator_base& i2)
		{
			// is i1 under i2?
			if (i1 == i2) return true;
			else if (i2.depth() >= i1.depth()) return false;
			// now we have i2.depth < i1.depth
			else return is_under(i1.parent(), i2);
		}
		
		bool root_die::advance_cu_context()
		{
			Dwarf_Unsigned seen_cu_header_length;
			Dwarf_Half seen_version_stamp;
			Dwarf_Unsigned seen_abbrev_offset;
			Dwarf_Half seen_address_size;
			Dwarf_Half seen_offset_size;
			Dwarf_Half seen_extension_size;
			Dwarf_Unsigned seen_next_cu_header;
			
			// if we don't have a dbg, return false straight away
			if (!dbg.handle) return false;
			
			int retval = dwarf_next_cu_header_b(dbg.handle.get(),
				&seen_cu_header_length, &seen_version_stamp, 
				&seen_abbrev_offset, &seen_address_size, 
				&seen_offset_size, &seen_extension_size,
				&seen_next_cu_header, &current_dwarf_error);
			assert(retval == DW_DLV_OK || retval == DW_DLV_NO_ENTRY);
			if (retval == DW_DLV_NO_ENTRY)
			{
				// this means no variables (including last_seen_next_cu_header) were set
				last_seen_cu_header_length = opt<Dwarf_Unsigned>(); assert(!last_seen_cu_header_length);
				last_seen_version_stamp = opt<Dwarf_Half>(); assert(!last_seen_version_stamp);
				last_seen_abbrev_offset = opt<Dwarf_Unsigned>(); assert(!last_seen_abbrev_offset);
				last_seen_address_size = opt<Dwarf_Half>(); assert(!last_seen_address_size);
				last_seen_offset_size = opt<Dwarf_Half>(); assert(!last_seen_offset_size);
				last_seen_extension_size = opt<Dwarf_Half>(); assert(!last_seen_extension_size);
				last_seen_next_cu_header = opt<Dwarf_Unsigned>(); assert(!last_seen_next_cu_header);
				current_cu_offset = 0UL;
				return false;
			}
			else
			{
				assert(retval == DW_DLV_OK);
				opt<Dwarf_Off> prev_next_cu_header = last_seen_next_cu_header;
				Dwarf_Off prev_current_cu_offset = current_cu_offset;
			
				last_seen_cu_header_length = seen_cu_header_length;
				last_seen_version_stamp = seen_version_stamp;
				last_seen_abbrev_offset = seen_abbrev_offset;
				last_seen_address_size = seen_address_size;
				last_seen_offset_size = seen_offset_size;
				last_seen_extension_size = seen_extension_size;
				last_seen_next_cu_header = seen_next_cu_header;
				
				// also grab the current CU DIE offset
				Die tmp_d(*this); // "current CU" constructor
				
				current_cu_offset = //iterator_base(std::move(tmp_handle), 1U, *this).offset_here();
					// can't use iterator_base because it will recursively try to make payload, make_cu_payload, 
					// set_cu_context, ...
					tmp_d.offset_here();
				
				if (prev_current_cu_offset == 0UL)
				{
					/* Assert sanity of first CU offset. */
					assert(current_cu_offset > 0UL && current_cu_offset < 32);
					first_cu_offset = opt<Dwarf_Off>(current_cu_offset);
				} 
				if (prev_next_cu_header) // sanity check
				{
					/* Note that next_cu_header is subtle:
					 * according to libdwarf,
					 * "the offset into the debug_info section of the next CU header",
					 * BUT it tends to be smaller than the value we got
					 * from offset_here(). */
					
					//assert(current_cu_offset == *prev_next_cu_header); // -- this is wrong
					
					assert(first_cu_offset);
					assert(current_cu_offset == *first_cu_offset
					 ||    current_cu_offset == (*prev_next_cu_header) + *first_cu_offset);
				}
				
				return true;
			}
		}
		bool root_die::clear_cu_context()
		{
			if (current_cu_offset == 0UL) return true;
			while(advance_cu_context());
			return true; // i.e. success
		}
		bool root_die::set_subsequent_cu_context(Dwarf_Off off)
		{
			if (current_cu_offset == off) return true;
			bool have_no_context = (current_cu_offset == 0UL);
			//else assert(!last_seen_next_cu_header);
			bool ret; //= last_seen_next_cu_header; // i.e. false if we have no current context
			do
			{
				ret = advance_cu_context();
				if (ret && have_no_context) { have_no_context = false; assert(current_cu_offset == 11); }
			}
			while (current_cu_offset != 0 // i.e. stop if we hit the no-context case
				&& current_cu_offset != off // i.e. stop if we reach our target
				&& ret); // i.e. stop if we fail to advance
			// begin sanity check
			if (ret)
			{
				Dwarf_Die test;
				Dwarf_Error temporary_error;
				int test_ret = dwarf_siblingof(dbg.handle.get(), nullptr, &test, &temporary_error);
				assert(test_ret == DW_DLV_OK);
				Dwarf_Off test_off;
				dwarf_dieoffset(test, &test_off, &temporary_error);
				assert(test_off == off);
			}
			// end sanity check
			return ret;
		}
		bool root_die::set_cu_context(Dwarf_Off off)
		{
			bool ret = set_subsequent_cu_context(off);
			if (!ret)
			{
				// we got to the end; try again
				clear_cu_context(); // should be a no-op
				return set_subsequent_cu_context(off);
				// FIXME: avoid repeated search
			}
			else return true;
		}
		iterator_base
		root_die::next_sibling(const iterator_base& it)
		{
			assert(&it.get_root() == this);
			if (!it.is_real_die_position()) return iterator_base::END;

			Dwarf_Off offset_here = it.offset_here();
			// check for cached edges 
			auto found = next_sibling_of.find(offset_here);
			if (found != next_sibling_of.end())
			{
				auto found_live = live_dies.find(found->second);
				if (found_live != live_dies.end())
				{
					return iterator_base(static_cast<abstract_die&&>(*found_live->second), it.depth(), *this);
				} // else fall through
			}
			
			auto found_parent = parent_of.find(offset_here);
			// if we issued `it', we should have recorded its parent
			// FIXME: relax this policy perhaps, to allow soft cache?
			assert(found_parent != parent_of.end());
			Dwarf_Off common_parent_offset = found_parent->second;
			Die::handle_type maybe_handle(nullptr, Die::deleter(nullptr)); // TODO: reenable deleter default constructor
			
			if (it.tag_here() == DW_TAG_compile_unit)
			{
				// do the CU thing
				bool ret = set_cu_context(it.offset_here());
				if (!ret) return iterator_base::END; // i.e. we're not a libdwarf-backed CU
				ret = advance_cu_context();
				if (!ret) return iterator_base::END;
				maybe_handle = Die::try_construct(*this);
				if (maybe_handle) next_sibling_of[it.offset_here()] = current_cu_offset;
			}
			else
			{
				// do the non-CU thing
				maybe_handle = Die::try_construct(*this, it);
			}
			
			// shared parent cache logic
			if (maybe_handle)
			{
				auto new_it = iterator_base(Die(std::move(maybe_handle)), it.get_depth(), *this);
				// install in parent cache
				parent_of[new_it.offset_here()] = common_parent_offset;
				next_sibling_of[offset_here] = new_it.offset_here();
				return new_it;
			} else return iterator_base::END;
		}
		
		bool 
		root_die::move_to_next_sibling(iterator_base& it)
		{
			// Dwarf_Off start_off = it.offset_here();
			unsigned start_depth = it.depth();
			auto maybe_sibling = next_sibling(it); 
			if (maybe_sibling != iterator_base::END) 
			{
				//debug(2) << "Think we found a later sibling of 0x" << std::hex << start_off
				//	<< " at 0x" << std::hex << maybe_sibling.offset_here() << std::dec << endl;
				it = std::move(maybe_sibling); assert(it.depth() == start_depth); return true; 
			}
			else return false;
		}
		
/* Here comes the factory. */
		root_die::ptr_type 
		root_die::make_payload(const iterator_base& it) // note: we update *mutable* fields
		{
			/* This call is asking us to heap-allocate the state of the iterator
			 * and upgrade the iterator so that it is copyable. There are some exceptions:
			 * root and END iterators have no handle, so they can be copied directly. */

			if (it.state == iterator_base::WITH_PAYLOAD) return it.cur_payload;
			else // we're a handle
			{
				assert(it.state == iterator_base::HANDLE_ONLY);

				// assert we're *not* sticky -- 
				// iterators with handles should not be created in the sticky case.
				// Whenever we construct an iterator, we build sticky payload if necessary.
				assert(!is_sticky(it.get_handle()));
				
				/* heap-allocate the right kind of basic_die, 
				 * creating the intrusive ptr, hence bumping the refcount */
				it.cur_payload = core::factory::for_spec(it.spec_here())
					.make_payload(std::move(it.get_handle()), *this);
				it.state = iterator_base::WITH_PAYLOAD;
				
				if (it.tag_here() != DW_TAG_compile_unit)
				{
					debug(2) << "Warning: made payload for non-CU at 0x" << std::hex << it.offset_here() << std::dec << endl;
				}
				return it.cur_payload;
			}
		}
		
		iterator_df<compile_unit_die>
		root_die::get_or_create_synthetic_cu()
		{
			if (this->synthetic_cu)
			{
				return find(*this->synthetic_cu).as_a<compile_unit_die>();
			}
			
			auto created = make_new(begin(), DW_TAG_compile_unit);
			/* FIXME: set attributes */
			
			this->synthetic_cu = created.offset_here();
			iterator_df<compile_unit_die> created_cu = created.as_a<compile_unit_die>();
			return created_cu;
		}
		
		iterator_base
		root_die::make_new(const iterator_base& parent, Dwarf_Half tag)
		{
			/* heap-allocate the right kind of (in-memory) DIE, 
			 * creating the intrusive ptr, hence bumping the refcount */
			auto& spec = parent.is_root_position() ? DEFAULT_DWARF_SPEC : parent.enclosing_cu().spec_here();
			root_die::ptr_type p = core::factory::for_spec(spec).make_new(parent, tag);
			Dwarf_Off o = dynamic_cast<in_memory_abstract_die&>(*p).get_offset();
			sticky_dies.insert(make_pair(o, p));
			assert(live_dies.find(o) != live_dies.end());
			parent_of.insert(make_pair(o, parent.offset_here()));
			auto found = find(o);
			assert(found);
			return found;
		}
		
		/* NOTE: I was thinking to put all factory code in namespace spec, 
		 * so that we can do 
		 * get_spec().factory(), 
		 * requiring all sub-namespace factories (lib::, encap::, core::)
		 * are centralised in spec
		 * (rather than trying to do core::factory<dwarf_current_def::inst>.make_payload(handle), 
		 * which wouldn't let us do get_spec().factory()...
		 * BUT
		 * core::factory_for(dwarf_current_def::inst).make_payload(handle) WOULD work. So
		 * it's a toss-up. Go with the latter. */
		bool root_die::is_sticky(const abstract_die& d)
		{
			/* This sets the default policy for stickiness: compile unit DIEs
			 * are sticky, but others aren't. Note that since we do the sticky
			 * test before making payload, we need to use raw libdwarf functions.
			 * We can't construct a Die because that means std::move(), and our 
			 * caller will still need the handle. 
			 * HMM -- now tried changing it so the caller passes us the Die. */
			
			return d.get_tag() == DW_TAG_compile_unit;
		}
		
		void
		root_die::get_referential_structure(
			unordered_map<Dwarf_Off, Dwarf_Off>& parent_of,
			map<pair<Dwarf_Off, Dwarf_Half>, Dwarf_Off>& refers_to) const
		{
			/* We walk the whole tree depth-first. 
			 * If we see any attributes that are references, we follow them. 
			 * Then we return our maps. */
			for (auto i = begin(); i != end(); ++i)
			{
				encap::attribute_map attrs = i.copy_attrs(); //(i.attrs_here(), i.get_handle(), *this);
				for (auto i_a = attrs.begin(); i_a != attrs.end(); ++i_a)
				{
					if (i_a->second.get_form() == encap::attribute_value::REF)
					{
						auto found = const_cast<root_die *>(this)->find(
							i_a->second.get_ref().off, 
							make_pair(i.offset_here(), i_a->first));
					}
				}
			}
			
			parent_of = this->parent_of;
			refers_to = this->refers_to;
		}
		
		Dwarf_Off root_die::fresh_cu_offset()
		{
			// what's our biggest CU offset right now? or just use the biggest sticky's enclosing CU
			if (sticky_dies.rbegin() == sticky_dies.rend())
			{
				first_child_of[0UL] = 1;
				parent_of[1] = 0UL;
				return 1;
			}
			
			Dwarf_Off biggest_cu_off = sticky_dies.rbegin()->second->get_offset();
			// in general, the biggest offset is the *last* item in depth-first order
			// FIXME: faster way to do this
			iterator_df<> i = cu_pos(biggest_cu_off);
			Dwarf_Off off = 0;
			for (; i != iterator_base::END; ++i)
			{
				off = i.offset_here();
			}
			assert(off != 0);
			next_sibling_of[biggest_cu_off] = off + 1;

			parent_of[off + 1] = 0UL;

			return off + 1;
		}
		
		Dwarf_Off root_die::fresh_offset_under(const iterator_base& pos)
		{
			// NOTE: maintain the invariant that relates offsets with topology
			iterator_df<> i = pos;
			auto children = i.children_here();
			auto prev_child = children.second;
			
			map<Dwarf_Off, Dwarf_Off> last_children_seen;
			
			std::function< iterator_df<>(const iterator_base&) > 
			highest_offset_iter_in_subtree
			 = [&highest_offset_iter_in_subtree, &last_children_seen](const iterator_base& t) {
				auto children = t.children_here();
				if (srk31::count(children.first, children.second) == 0)
				{
					return iterator_df<>(t);
				}
				else
				{
					auto last_sib = children.first;
					while (++children.first != children.second)
					{
						last_sib = children.first;
					}
					last_children_seen[t.offset_here()] = last_sib.offset_here();
					return highest_offset_iter_in_subtree(last_sib);
				}
			};

			/*              d                                    e
			     ,-----,----.------.-----.                     / | \
			    c1     c2   c3     c4    c5
			   '  '   '  '  ' '    ' '   ' '
			      gk1    gk2
			   We want to traverse in depth-first order
			   and find gaps
			 */
			
			/* get the highest offset pos in the subtree, and compare it 
			   against the sibling subtree
			 */
			iterator_df<> highest_offset_pos = highest_offset_iter_in_subtree(pos);
			iterator_df<> next = highest_offset_pos; ++next;
			
			Dwarf_Off offset_to_issue;
			if (!next)
			{
				// there's no later subtree
				offset_to_issue = highest_offset_pos.offset_here() + 1;
			} 
			else if (next.offset_here() - highest_offset_pos.offset_here() > 1)
			{
				offset_to_issue = highest_offset_pos.offset_here() + 1;
			}
			else
			{
				// no more room!
				assert(false);
			}
			
			// are we issuing a first child or a next sibling?
			if (highest_offset_pos.offset_here() == pos.offset_here())
			{
				// we're issuing a first child
				first_child_of[pos.offset_here()] = offset_to_issue;
			}
			else
			{
				// we're issuing a next sibling of the currently-last sibling
				auto found_last_sib = last_children_seen.find(pos.offset_here());
				assert(found_last_sib != last_children_seen.end());
				next_sibling_of[found_last_sib->second] = offset_to_issue;
			}
			
			parent_of[offset_to_issue] = pos.offset_here();
			
			return offset_to_issue;
		}

	}
}
