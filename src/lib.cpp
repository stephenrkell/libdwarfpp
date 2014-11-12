/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * lib.cpp: basic C++ wrapping of libdwarf C API.
 *
 * Copyright (c) 2008--9, Stephen Kell.
 */

#include "dwarfpp/lib.hpp"
#include "dwarfpp/adt.hpp"
#include "dwarfpp/encap.hpp" // re-use some formatting logic in encap, for convenience
	// FIXME: flip the above around, so that the formatting logic is in here!
#include "dwarfpp/expr.hpp" /* for absolute_loclist_to_additive_loclist */
#include "dwarfpp/frame.hpp"

#include <srk31/indenting_ostream.hpp>
#include <srk31/algorithm.hpp>
#include <sstream>
#include <libelf.h>
#include <cstring> /* We use strcmp in linear search-by-name -- likely this will change */ 

namespace dwarf
{
	namespace core
	{
		using std::pair;
		using std::make_pair;
		using std::string;
		using boost::optional;
		using std::unique_ptr;
		using std::deque;
		using std::map;
		using std::endl;
		using namespace dwarf::lib;
		
#ifndef NO_TLS
		__thread Dwarf_Error current_dwarf_error;
#else
		Dwarf_Error current_dwarf_error;
#endif
		
		void exception_error_handler(Dwarf_Error error, Dwarf_Ptr errarg)
		{
			throw Error(error, errarg); // FIXME: saner non-copying interface? 
		}
		
		Debug::Debug(int fd)
		{
			Dwarf_Debug returned;
			int ret = dwarf_init(fd, DW_DLC_READ, exception_error_handler, 
				nullptr, &returned, &current_dwarf_error);
			assert(ret == DW_DLV_OK);
			this->handle = handle_type(returned);
		}
		
		Debug::Debug(Elf *elf)
		{
			Dwarf_Debug returned;
			int ret = dwarf_elf_init(reinterpret_cast<dwarf::lib::Elf_opaque_in_libdwarf*>(elf), 
				DW_DLC_READ, exception_error_handler, 
				nullptr, &returned, &current_dwarf_error);
			assert(ret == DW_DLV_OK);
			this->handle = handle_type(returned);
		}
		
		void 
		Debug::deleter::operator()(raw_handle_type arg) const
		{
			dwarf_finish(arg, &current_dwarf_error);
		}
		
		/* printing of basic_dies */
		
		void basic_die::print(std::ostream& s) const
		{
			s << "DIE, offset 0x" << std::hex << get_offset() << std::dec
				<< ", tag " << this->s.tag_lookup(get_tag());
			if (get_name()) s << ", name \"" << *get_name() << "\""; 
			else s << ", no name";
		}
		void basic_die::print_with_attrs(std::ostream& s, optional_root_arg_decl) const
		{
			s << "DIE, offset 0x" << std::hex << get_offset() << std::dec
				<< ", tag " << this->s.tag_lookup(get_tag())
				<< ", attributes: ";
			srk31::indenting_ostream is(s);
			is.inc_level();
			is << endl << copy_attrs(get_root(opt_r));
			is.dec_level();
		}
		/* print a single DIE */
		std::ostream& operator<<(std::ostream& s, const basic_die& d)
		{
			d.print(s);
			return s;
		}
		
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
				auto m = copy_attrs(get_root());
				m.print(s, indent_level + 1);
				//is << endl << copy_attrs(get_root());
				//is.dec_level();
			}
		}
		std::ostream& operator<<(std::ostream& s, const iterator_base& i)
		{
			i.print(s);
			return s;
		}
		
		/* print a whole tree of DIEs -- only defined on iterators. 
		 * If we made it a method on iterator_base, it would have
		 * to copy itself. f we make it a method on root_die, it does
		 * not have to copy. */
		void root_die::print_tree(iterator_base&& begin, std::ostream& s) const
		{
			unsigned start_depth = begin.m_depth;
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

		string abstract_die::summary() const
		{
			std::ostringstream s;
			s << "At 0x" << std::hex << get_offset() << std::dec
						<< ", tag " << dwarf::spec::DEFAULT_DWARF_SPEC.tag_lookup(get_tag());
			return s.str();
		}
		std::ostream& operator<<(std::ostream& s, const AttributeList& attrs)
		{
			for (int i = 0; i < attrs.get_len(); ++i)
			{
				s << dwarf::spec::DEFAULT_DWARF_SPEC.attr_lookup(attrs[i].attr_here());
				try
				{
					encap::attribute_value v(attrs[i], attrs.d, attrs.d.get_constructing_root());
					s << ": " << v;
				} catch (Not_supported)
				{
					//s << "(unknown attribute 0x" << std::hex << attrs[i].attr_here() 
					//	<< std::dec << ")";
				}
				s << endl;
			}
			return s;
		}
		
		/* Individual attribute access within basic_die */
		encap::attribute_map  basic_die::all_attrs(optional_root_arg_decl) const
		{
			return copy_attrs(opt_r);
		}
		encap::attribute_value basic_die::attr(Dwarf_Half a, optional_root_arg_decl) const
		{
			Attribute attr(d, a);
			return encap::attribute_value(attr, d, get_root(opt_r));
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
		encap::attribute_map basic_die::find_all_attrs(optional_root_arg_decl) const
		{
			encap::attribute_map m = copy_attrs(get_root(opt_r));
			// merge with attributes of abstract_origin and specification
			if (has_attr(DW_AT_abstract_origin))
			{
				encap::attribute_map origin_m = attr(DW_AT_abstract_origin, opt_r).get_refiter()->all_attrs();
				left_merge_attrs(m, origin_m);
			}
			else if (has_attr(DW_AT_specification))
			{
				encap::attribute_map origin_m = attr(DW_AT_specification, opt_r).get_refiter()->all_attrs();
				left_merge_attrs(m, origin_m);
			}
			else if (has_attr(DW_AT_declaration))
			{
				/* How do we get to the "real" DIE from this specification? The 
				 * specification attr doesn't tell us, so we have to search. */
				iterator_df<> found = find_definition(opt_r);
				if (found) left_merge_attrs(m, found->all_attrs());
			}
			return m;
		}
		encap::attribute_value basic_die::find_attr(Dwarf_Half a, optional_root_arg_decl) const
		{
			if (has_attr(a)) { return attr(a, get_root(opt_r)); }
			else if (has_attr(DW_AT_abstract_origin))
			{
				return attr(DW_AT_abstract_origin, opt_r).get_refiter()->find_attr(a, opt_r);
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

				auto decl = attr(DW_AT_specification, opt_r).get_refiter();
				if (decl.has_attr(a)) return decl->attr(a, opt_r);
			}
			else if (has_attr(DW_AT_declaration))
			{
				/* How do we get to the "real" DIE from this declaration? The 
				 * declaration attr doesn't tell us, so we have to search.. */
				iterator_df<> found = find_definition(opt_r);
				if (found && found.offset_here() != get_offset()) return found->find_attr(a, opt_r);
			}
			return encap::attribute_value(); // a.k.a. a NO_ATTR-valued attribute_value
		}
		iterator_base basic_die::find_definition(optional_root_arg_decl) const
		{
			/* For most DIEs, we just return ourselves if we don't have DW_AT_specification
			 * and nothing if we do. */
			if (has_attr(DW_AT_specification) && 
				attr(DW_AT_specification, opt_r).get_flag())
			{
				return iterator_base::END;
			} else return get_root(opt_r).find(get_offset()); // we have to find ourselves :-(
		}
		
		root_die::root_die(int fd)
		 : dbg(fd), p_fs(new FrameSection(get_dbg(), true)), 
		   current_cu_offset(0UL), returned_elf(nullptr), first_cu_offset(),
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
					cerr << "Warning: searching for parent of " << it << " all the way from root." << endl;
					auto found_again = find_downwards(it.offset_here());
					found = parent_of.find(it.offset_here());
				}
				assert(found != parent_of.end());
				assert(found->first == it.offset_here());
				assert(found->second < it.offset_here());
				//cerr << "Parent cache says parent of 0x" << std::hex << found->first
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
				return pos(found->second, it.depth() - 1, optional<Dwarf_Off>());
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
				auto found_sticky = sticky_dies.find(found->second);
				if (found_sticky != sticky_dies.end())
				{
					return iterator_base(static_cast<abstract_die&&>(*found_sticky->second), it.depth() + 1, *this);
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
				iterator_base new_it(Die(std::move(maybe_handle)), it.get_depth() + 1, it.get_root());
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
					return p_with->named_child(name, *p_root);
				}
			}
			return p_root->find_named_child(*this, name);
		}
		
		iterator_base
		root_die::find_named_child(const iterator_base& start, const string& name)
		{
			auto children = start.children_here();
			for (auto i_child = std::move(children.first); i_child != children.second; ++i_child)
			{
				// /* Use strcmp because name_here returns us a fancy char*, not a std::string. */
				//if (i_child.get_raw_name() != nullptr
				// 	&& 0 == strcmp(i_child.get_raw_name().get(), name.c_str()))
				if (i_child.name_here() && *i_child.name_here() == name)
				{
					return std::move(i_child);
				}
			}
			return iterator_base::END;
		}
		
		iterator_base
		root_die::find_visible_named_grandchild(const string& name)
		{
			vector<string> name_vec(1, name);
			std::vector<iterator_base > found;
			resolve_all_visible_from_root(name_vec.begin(), name_vec.end(), 
				found, 1);
			return found.size() > 0 ? *found.begin() : iterator_base::END;
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
			// boost::optional doesn't let us get a writable lvalue out of an
			// uninitialized optional, so we have to dummy up. 

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
				last_seen_cu_header_length = optional<decltype(*last_seen_cu_header_length)>(); assert(!last_seen_cu_header_length);
				last_seen_version_stamp = optional<decltype(*last_seen_version_stamp)>(); assert(!last_seen_version_stamp);
				last_seen_abbrev_offset = optional<decltype(*last_seen_abbrev_offset)>(); assert(!last_seen_abbrev_offset);
				last_seen_address_size = optional<decltype(*last_seen_address_size)>(); assert(!last_seen_address_size);
				last_seen_offset_size = optional<decltype(*last_seen_offset_size)>(); assert(!last_seen_offset_size);
				last_seen_extension_size = optional<decltype(*last_seen_extension_size)>(); assert(!last_seen_extension_size);
				last_seen_next_cu_header = optional<decltype(*last_seen_next_cu_header)>(); assert(!last_seen_next_cu_header);
				current_cu_offset = 0UL;
				return false;
			}
			else
			{
				assert(retval == DW_DLV_OK);
				optional<Dwarf_Off> prev_next_cu_header = last_seen_next_cu_header;
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
					first_cu_offset = optional<Dwarf_Off>(current_cu_offset);
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
				auto found_sticky = sticky_dies.find(found->second);
				if (found_sticky != sticky_dies.end())
				{
					return iterator_base(static_cast<abstract_die&&>(*found_sticky->second), it.depth(), *this);
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
				//cerr << "Think we found a later sibling of 0x" << std::hex << start_off
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
					.make_payload(std::move(dynamic_cast<Die&>(it.get_handle()).handle), *this);
				it.state = iterator_base::WITH_PAYLOAD;
				
#ifdef DWARFPP_WARN_ON_INEFFICIENT_USAGE
				if (it.tag_here() != DW_TAG_compile_unit)
				{
					cerr << "Warning: made payload for non-CU at 0x" << std::hex << it.offset_here() << std::dec << endl;
				}
#endif
				return it.cur_payload;
			}
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
			parent_of.insert(make_pair(o, parent.offset_here()));
			auto found = find(o);
			assert(found);
			return found;
		}
		
		/* NOTE: I was thinking to put all factory code in namespace spec, 
		 * so that we can do 
		 * get_spec(r).factory(), 
		 * requiring all sub-namespace factories (lib::, encap::, core::)
		 * are centralised in spec
		 * (rather than trying to do core::factory<dwarf3_def::inst>.make_payload(handle), 
		 * which wouldn't let us do get_spec(r).factory()...
		 * BUT
		 * core::factory_for(dwarf3_def::inst).make_payload(handle) WOULD work. So
		 * it's a toss-up. Go with the latter. */
				
		dwarf3_factory_t dwarf3_factory;
		basic_die *dwarf3_factory_t::make_non_cu_payload(Die::handle_type&& h, root_die& r)
		{
			basic_die *p;
			Die d(std::move(h));
			assert(d.tag_here() != DW_TAG_compile_unit);
			switch (d.tag_here())
			{
#define factory_case(name, ...) \
case DW_TAG_ ## name: p = new name ## _die(d.spec_here(r), std::move(d.handle)); break; // FIXME: not "basic_die"...
#include "dwarf3-factory.h"
#undef factory_case
				default: p = new basic_die(d.spec_here(r), std::move(d.handle)); break;
			}
			return p;
		}
		
		compile_unit_die *factory::make_cu_payload(Die::handle_type&& h, root_die& r)
		{
			// Key point: we're not allowed to call Die::spec_here() for this handle. 
			// We could write libdwarf-level code to grab the version stamp and 
			// so on... for now, just construct the thing.
			Die d(std::move(h));
			Dwarf_Off off = d.offset_here();
			auto p = new compile_unit_die(dwarf::spec::dwarf3, std::move(d.handle));
			/* fill in the CU fields -- this code would be shared by all 
			 * factories, so we put it here (but HMM, if our factories were
			 * a delegation chain, we could just put it in the root). */

			bool ret = r.set_cu_context(off);
			assert(ret);

			p->cu_header_length = *r.last_seen_cu_header_length;
			p->version_stamp = *r.last_seen_version_stamp;
			p->abbrev_offset = *r.last_seen_abbrev_offset;
			p->address_size = *r.last_seen_address_size;
			p->offset_size = *r.last_seen_offset_size;
			p->extension_size = *r.last_seen_extension_size;
			p->next_cu_header = *r.last_seen_next_cu_header;
			
			return p;
		}
		
		// like make_cu_payload, but we're called by the root_die to make a whole fresh CU
		// FIXME: this 
		compile_unit_die *factory::make_new_cu(root_die& r, std::function<compile_unit_die*()> constructor)
		{
			auto p = constructor();
			//new compile_unit_die(dwarf::spec::dwarf3, Die::handle_type(nullptr, nullptr));
			// FIXME FIXME FIXME FIXME FIXME FIXME FIXME
			p->cu_header_length = 1;
			p->version_stamp = 2;
			p->abbrev_offset = 0;
			p->address_size = sizeof (void*);
			p->offset_size = sizeof (void*);
			p->extension_size = sizeof (void*);
			p->next_cu_header = 0;
			
			// NOTE: the DIE we just created does not even have an offset
			// until it is added to the sticky set of the containing root DIE
			
			return p;
		}
		
		basic_die *factory::make_new(const iterator_base& parent, Dwarf_Half tag)
		{
			root_die& r = parent.root();
// declare all the in-memory structs as local classes (for now)
#define factory_case(name, ...) \
			struct in_memory_ ## name ## _die : public in_memory_abstract_die, \
			     public name ## _die \
			{ \
				in_memory_ ## name ## _die(const iterator_base& parent) \
				: \
					/* initialize basic_die directly, since it's a virtual base */ \
					basic_die(parent.depth() >= 1 ? parent.spec_here() : DEFAULT_DWARF_SPEC), \
					in_memory_abstract_die(parent.root(), \
							  parent.is_root_position() ? \
							  parent.root().fresh_cu_offset() \
							: parent.root().fresh_offset_under(/*parent.root().enclosing_cu(parent)*/parent), \
						parent.is_root_position() ? 0 : parent.enclosing_cu_offset_here(), \
						DW_TAG_ ## name), \
					name ## _die(parent.depth() >= 1 ? parent.spec_here() : DEFAULT_DWARF_SPEC) \
				{ if (parent.is_root_position()) m_cu_offset = m_offset; } \
				root_die& get_root(opt<root_die&> opt_r) const \
				{ return *p_root; } \
				/* We also (morally redundantly) override all the abstract_die methods 
				 * to call the in_memory_abstract_die versions, in order to 
				 * provide a unique final overrider. */ \
				Dwarf_Off get_offset() const \
				{ return this->in_memory_abstract_die::get_offset(); } \
				Dwarf_Half get_tag() const \
				{ return this->in_memory_abstract_die::get_tag(); } \
				opt<string> get_name() const \
				{ return this->in_memory_abstract_die::get_name(); } \
				Dwarf_Off get_enclosing_cu_offset() const \
				{ return this->in_memory_abstract_die::get_enclosing_cu_offset(); } \
				bool has_attr(Dwarf_Half attr) const \
				{ return this->in_memory_abstract_die::has_attr(attr); } \
				encap::attribute_map copy_attrs(opt<root_die&> opt_r) const \
				{ return this->in_memory_abstract_die::copy_attrs(opt_r); } \
				inline spec& get_spec(root_die& r) const \
				{ return this->in_memory_abstract_die::get_spec(r); } \
				/* also override the wider attribute API from basic_die */ \
				/* get all attrs in one go */ \
				virtual encap::attribute_map all_attrs(optional_root_arg) const \
				{ return copy_attrs(opt_r); } \
				/* get a single attr */ \
				virtual encap::attribute_value attr(Dwarf_Half a, optional_root_arg) const \
				{ if (has_attr(a)) return m_attrs.find(a)->second; else assert(false); } \
				/* get all attrs in one go, seeing through abstract_origin / specification links */ \
				/* -- this one should work already: virtual encap::attribute_map find_all_attrs(optional_root_arg) const; */ \
				/* get a single attr, seeing through abstract_origin / specification links */ \
				/* -- this one should work already: virtual encap::attribute_value find_attr(Dwarf_Half a, optional_root_arg) const; */ \
			};
#include "dwarf3-factory.h"
#undef factory_case

			if (tag == DW_TAG_compile_unit)
			{
				return make_new_cu(r, [parent](){ return new in_memory_compile_unit_die(parent); });
			}
			
			if (parent.depth() == 1) r.visible_named_grandchildren_is_complete = false;
			
			//Dwarf_Off parent_off = parent.offset_here();
			//Dwarf_Off new_off = /*parent.is_root_position() ? r.fresh_cu_offset() : */ r.fresh_offset_under(r.enclosing_cu(parent));
			//Dwarf_Off cu_off = /*(tag == DW_TAG_compile_unit) ? new_off : */ parent.enclosing_cu_offset_here();
			
			switch (tag)
			{
#define factory_case(name, ...) \
case DW_TAG_ ## name: \
			return new in_memory_ ## name ## _die(parent);
#include "dwarf3-factory.h"
				default: return nullptr;
			}
#undef factory_case
		}
		
		basic_die *dwarf3_factory_t::dummy_for_tag(Dwarf_Half tag)
		{
			static basic_die dummy_basic(dwarf::spec::dwarf3);
#define factory_case(name, ...) \
static name ## _die dummy_ ## name(dwarf::spec::dwarf3); 
#include "dwarf3-factory.h"
#undef factory_case
			switch (tag)
			{
#define factory_case(name, ...) \
case DW_TAG_ ## name: return &dummy_ ## name;
#include "dwarf3-factory.h"
#undef factory_case
				default: return &dummy_basic;
			}
		}
		
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
			map<Dwarf_Off, Dwarf_Off>& parent_of,
			map<pair<Dwarf_Off, Dwarf_Half>, Dwarf_Off>& refers_to) const
		{
			/* We walk the whole tree depth-first. 
			 * If we see any attributes that are references, we follow them. 
			 * Then we return our maps. */
			for (auto i = begin(); i != end(); ++i)
			{
				encap::attribute_map attrs = i.copy_attrs(const_cast<root_die&>(*this)); //(i.attrs_here(), i.get_handle(), *this);
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
		
		Dwarf_Off Die::offset_here() const
		{
			Dwarf_Off off;
			int ret = dwarf_dieoffset(raw_handle(), &off, &current_dwarf_error);
			assert(ret == DW_DLV_OK);
			return off;
		}
		Dwarf_Off iterator_base::offset_here() const
		{
			if (!is_real_die_position()) { assert(is_root_position()); return 0; }
			return get_handle().get_offset();
		}
		
		Dwarf_Half Die::tag_here() const
		{
			Dwarf_Half tag;
			int ret = dwarf_tag(handle.get(), &tag, &current_dwarf_error);
			assert(ret == DW_DLV_OK);
			return tag;
		}
		Dwarf_Half iterator_base::tag_here() const
		{
			if (!is_real_die_position()) return 0;
			return get_handle().get_tag();
		}
		std::unique_ptr<const char, string_deleter>
		Die::name_here() const
		{
			char *str;
			int ret = dwarf_diename(raw_handle(), &str, &current_dwarf_error);
			if (ret == DW_DLV_NO_ENTRY) return nullptr;
			if (ret == DW_DLV_OK) return unique_ptr<const char, string_deleter>(
				str, string_deleter(get_dbg()));
			assert(false);
		}
		//std::unique_ptr<const char, string_deleter>
		opt<string>
		iterator_base::name_here() const
		{
			if (!is_real_die_position()) return nullptr;
			return get_handle().get_name();
		}
		bool Die::has_attr_here(Dwarf_Half attr) const
		{
			Dwarf_Bool returned;
			int ret = dwarf_hasattr(raw_handle(), attr, &returned, &current_dwarf_error);
			assert(ret == DW_DLV_OK);
			return returned;
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
		// for nearest_enclosing(DW_TAG_compile_unit), libdwarf supplies a call:
		// dwarf_CU_dieoffset_given_die -- gives you the CU of a DIE
		Dwarf_Off Die::enclosing_cu_offset_here() const
		{
			Dwarf_Off cu_offset;
			int ret = dwarf_CU_dieoffset_given_die(raw_handle(),
				&cu_offset, &current_dwarf_error);
			if (ret == DW_DLV_OK) return cu_offset;
			else assert(false);
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
		
		Dwarf_Half Attribute::attr_here() const
		{
			Dwarf_Half attr; 
			int ret = dwarf_whatattr(handle.get(), &attr, &current_dwarf_error);
			assert(ret == DW_DLV_OK);
			return attr;
		}			
		
		Dwarf_Half Attribute::form_here() const
		{
			Dwarf_Half form; 
			int ret = dwarf_whatform(handle.get(), &form, &current_dwarf_error);
			assert(ret == DW_DLV_OK);
			return form;
		}
		
		// FIXME: turn some of the above into inlines
		
	} /* end namespace core */

	namespace lib
	{
		void default_error_handler(Dwarf_Error e, Dwarf_Ptr errarg) 
		{ 
			//fprintf(stderr, "DWARF error!\n"); /* this is the C version */
			/* NOTE: when called by a libdwarf function,
			 * errarg is always the Dwarf_Debug concerned with the error */
        	std::cerr << "libdwarf error!" << std::endl;
			throw Error(e, errarg); /* Whoever catches this should also dwarf_dealloc the Dwarf_Error_s. */
		}

		file::file(int fd, Dwarf_Unsigned access /*= DW_DLC_READ*/,
			Dwarf_Ptr errarg /*= 0*/,
			Dwarf_Handler errhand /*= default_error_handler*/,
			Dwarf_Error *error /*= 0*/) : fd(fd)
		{
    		if (error == 0) error = &last_error;
			if (errarg == 0) errarg = this;
			int retval;
			retval = dwarf_init(fd, access, errhand, errarg, &dbg, error);
			have_cu_context = false;
            switch (retval)
            {
            case DW_DLV_ERROR:
            	/* From libdwarf docs:
                 * "An Dwarf_Error returned from dwarf_init() or dwarf_elf_init() 
                 * in case of a failure cannot be freed using dwarf_dealloc(). 
                 * The only way to free the Dwarf_Error from either of those calls
                 * is to use free(3) directly. Every Dwarf_Error must be freed by 
                 * dwarf_dealloc() except those returned by dwarf_init() or 
                 * dwarf_elf_init()." 
                 * 
                 * This means we shouldn't pass the Dwarf_Error through the 
                 * exception that our error handler generates, because the
                 * catching code will try to dwarf_dealloc() it when it should
                 * instead free() it. HACK: for now, free() it here.
                 * ANOTHER HACK: no, don't, because this is causing problems
                 * in libprocessimage. */
                /* free(error);*/
            	default_error_handler(NULL, errarg); // throws
                break;
            case DW_DLV_NO_ENTRY:
            	throw No_entry();
            default:
            	break; // fall through
            }
            free_elf = false;
            elf = 0;
            try
            {
            	this->p_aranges = new aranges(*this);
            }
            catch (No_entry)
            {
            	// the file must not have a .debug_aranges section
                this->p_aranges = 0;
            }
		}

		file::~file()
		{
			//Elf *elf; // maybe this Elf business is only for dwarf_elf_open?
			//int retval;
			//retval = dwarf_get_elf(dbg, &elf, &last_error);
			if (dbg != 0) dwarf_finish(dbg, &last_error);
			if (free_elf) elf_end(reinterpret_cast< ::Elf*>(elf));
            if (p_aranges) delete p_aranges;
		}

    	int file::next_cu_header(
	    	Dwarf_Unsigned *cu_header_length,
	    	Dwarf_Half *version_stamp,
	    	Dwarf_Unsigned *abbrev_offset,
	    	Dwarf_Half *address_size,
	    	Dwarf_Unsigned *next_cu_header,
	    	Dwarf_Error *error /*=0*/)
    	{
	    	if (error == 0) error = &last_error;
			int retval = dwarf_next_cu_header(dbg, cu_header_length, version_stamp,
        			abbrev_offset, address_size, next_cu_header, error); // may allocate **error
			have_cu_context = true;
			return retval;
    	}

		int file::clear_cu_context(cu_callback_t cb, void *arg)
		{
			if (!have_cu_context) return DW_DLV_OK;
		
			int retval;
			Dwarf_Unsigned cu_header_length;
			Dwarf_Half version_stamp;
			Dwarf_Unsigned abbrev_offset;
			Dwarf_Half address_size;
			Dwarf_Unsigned next_cu_header;
			//std::cerr << "Resetting CU state..." << std::endl;
			while(DW_DLV_OK == (retval = advance_cu_context(
				&cu_header_length, &version_stamp, 
				&abbrev_offset, &address_size, 
				&next_cu_header,
				cb, arg)));

			have_cu_context = false;
			//std::cerr << "next_cu_header returned " << retval << std::endl;
			if (retval == DW_DLV_NO_ENTRY)
			{
				/* This is okay -- means we iterated to the end of the CUs
				 * and are now back in the beginning state, which is what we want. */
				return DW_DLV_OK;
			}
			else return retval;
		}
		
		int file::advance_cu_context(Dwarf_Unsigned *cu_header_length,
				Dwarf_Half *version_stamp,
				Dwarf_Unsigned *abbrev_offset,
				Dwarf_Half *address_size, 
				Dwarf_Unsigned *next_cu_header,
				cu_callback_t cb, void *arg)
		{
			/* All the output parameters are optional. 
			 * BUT we *always* call the callback with the full set! 
			 * So we need to dummy the pointers. */
			Dwarf_Unsigned dummy_cu_header_length,
			 *real_cu_header_length = cu_header_length ? cu_header_length : &dummy_cu_header_length;
			
			Dwarf_Half dummy_version_stamp,
			 *real_version_stamp = version_stamp ? version_stamp : &dummy_version_stamp;
			
			Dwarf_Unsigned dummy_abbrev_offset,
			 *real_abbrev_offset = abbrev_offset ? abbrev_offset : &dummy_abbrev_offset;
			
			Dwarf_Half dummy_address_size,
			 *real_address_size = address_size ? address_size : &dummy_address_size;
			
			Dwarf_Unsigned dummy_next_cu_header,
			 *real_next_cu_header = next_cu_header ? next_cu_header : &dummy_next_cu_header;
			
			int retval = this->next_cu_header(
				real_cu_header_length, real_version_stamp, 
				real_abbrev_offset, real_address_size, 
				real_next_cu_header);
			if (retval == DW_DLV_OK)
			{
				have_cu_context = true;
				//std::cerr << "next_cu_header returned DW_DLV_OK" << std::endl;
				die d(*this); // get the CU die
				Dwarf_Off off; 
				int retval = d.offset(&off); // this should not fail
				assert(retval == DW_DLV_OK);
				if (cb) cb(arg, off, 
					*real_cu_header_length, *real_version_stamp,
					*real_abbrev_offset, *real_address_size, *real_next_cu_header);
			}
			else if (retval == DW_DLV_NO_ENTRY) have_cu_context = false;
			
			if (cu_header_length) *cu_header_length = *real_cu_header_length;
			if (version_stamp) *version_stamp = *real_version_stamp;
			if (abbrev_offset) *abbrev_offset = *real_abbrev_offset;
			if (address_size) *address_size = *real_address_size;
			if (next_cu_header) *next_cu_header = *real_next_cu_header;
			
			return retval;
		}


		int file::get_elf(Elf **out_elf, Dwarf_Error *error /*= 0*/)        	
        { 
        	if (elf != 0) { *out_elf = elf; return DW_DLV_OK; }
        	if (error == 0) error = &last_error;
            int retval = dwarf_get_elf(dbg, 
            	reinterpret_cast<dwarf::lib::dwarf_elf_handle*>(&elf), 
                error);
            *out_elf = elf;
            return retval;
        }

		int file::siblingof(
			die& d,
			die *return_sib,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = &last_error;
			return dwarf_siblingof(dbg,
				d.my_die,
				&(return_sib->my_die),
				error); // may allocate **error, allocates **return_sib? 
		}

		int file::first_die(
			die *return_die,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = &last_error;
			ensure_cu_context();
			return dwarf_siblingof(dbg,
				NULL,
				&(return_die->my_die),
				error); // may allocate **error, allocates **return_sib? 
		} // special case of siblingof

		int file::offdie(
			Dwarf_Off offset,
			die *return_die,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = &last_error;
			return dwarf_offdie(dbg,
				offset,
				&(return_die->my_die),
				error); // may allocate **error, allocates **return_die?
		}

	// 	int file::get_globals(
	// 		global_array *& globarr, // the client passes 
	// 		Dwarf_Error *error /*= 0*/)
	// 	{
	// 		if (error == 0) error = &last_error;
	// 		Dwarf_Signed cnt;
	// 		Dwarf_Global *globals;
	// 		int retval = dwarf_get_globals(
	// 			dbg, &globals, &cnt, error
	// 		);
	// 		assert(retval == DW_DLV_OK);
	// 		globarr = new global_array(dbg, globals, cnt);
	// 		return retval;
	// 	} // allocates globarr, else allocates **error

		int file::get_cu_die_offset_given_cu_header_offset(
			Dwarf_Off in_cu_header_offset,
			Dwarf_Off * out_cu_die_offset,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = &last_error;
			return DW_DLV_ERROR; // TODO: implement this
		}
		
		//file die::dummy_file; // static, should be const (FIXME)

		die::die(file& f, Dwarf_Die d, Dwarf_Error *perror) : f(f), p_last_error(perror)
		{
			this->my_die = d;
		}
		die::~die()
		{
			dwarf_dealloc(f.dbg, my_die, DW_DLA_DIE);
		}

		int die::first_child(
			die *return_kid,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			/* If we have a null my_die, it means we are a file_toplevel_die
			 * and are being asked for the first CU. */
			if (!my_die)
			{
				f.clear_cu_context();
				int retval = f.advance_cu_context();
				if (retval == DW_DLV_NO_ENTRY) throw No_entry();
				// now at first CU
				retval = f.first_die(return_kid, error);
				assert(retval != DW_DLV_NO_ENTRY); // CU header found implies CU DIE found
				return retval;
			}
			else return dwarf_child(my_die, &(return_kid->my_die), error);
		} // may allocate **error, allocates *(return_kid->my_die) on return

		int die::tag(
			Dwarf_Half *tagval,
			Dwarf_Error *error /*= 0*/) const
		{
			if (error == 0) error = p_last_error;
			return dwarf_tag(my_die, tagval, error);
		} // may allocate **error

		int die::offset(
			Dwarf_Off * return_offset,
			Dwarf_Error *error /*= 0*/) const
		{
			if (error == 0) error = p_last_error;
			return dwarf_dieoffset(my_die, return_offset, error);
		} // may allocate **error

		int die::CU_offset(
			Dwarf_Off *return_offset,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_die_CU_offset(my_die, return_offset, error);
		} // may allocate **error

		int die::name(
			std::string *return_name,
			Dwarf_Error *error /*= 0*/) const
		{
			if (error == 0) error = p_last_error;
			char *returned_name_chars;
			int retval = dwarf_diename(my_die, &returned_name_chars, error);
			if (retval == DW_DLV_OK)
			{
				*return_name = returned_name_chars; // HACK: copying string is not okay,
				 // but we are undertaking to provide a RAII inerface here
				 // -- arguably we should provide a class dwarf::lib::diename
				dwarf_dealloc(f.dbg, returned_name_chars, DW_DLA_STRING);
			}
			//std::cerr << "Retval from dwarf_diename is " << retval << std::endl;
			return retval;
		} // may allocate **name, else may allocate **error

	// 	int die::attrlist(
	// 		attribute_array *& attrbuf, // on return, attrbuf points to an attribute_array
	// 		Dwarf_Error *error /*= 0*/)
	// 	{
	// 		if (error == 0) error = p_last_error;
	// 		Dwarf_Signed cnt;
	// 		Dwarf_Attribute *attrs;
	// 		int retval = dwarf_attrlist(
	// 			my_die, &attrs, &cnt, error
	// 		);
	// 		assert(retval == DW_DLV_OK);
	// 		attrbuf = new attribute_array(f.get_dbg(), attrs, cnt);
	// 		return retval;
	// 	} // allocates attrbuf; else may allocate **error
	// 	/* TODO: delete this, now we have an attribute_array instead */

		int die::hasattr(Dwarf_Half attr, Dwarf_Bool *return_bool, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			if (return_bool) *return_bool = 0; /* to avoid risk of undefinedness 
			 * from dodgy code like the attr_optional macro in adt.h
			 * which does not test the return value. */
			return dwarf_hasattr(my_die, attr, return_bool, error);		
		} // may allocate **error

	// 	int die::attr(Dwarf_Half attr, attribute *return_attr, Dwarf_Error *error /*= 0*/)
	// 	{
	// 		if (error == 0) error = p_last_error;
	// 		Dwarf_Attribute tmp_attr;
	// 		int retval = dwarf_attr(my_die, attr, &tmp_attr, error);
	// 		assert(retval == DW_DLV_OK);
	// 		return_attr = new attribute(tmp_attr);
	// 		return retval;
	// 	} // allocates *return_attr
	// TODO: document this: we only support getting *all* attributes, not individual ones

		int die::lowpc(Dwarf_Addr * return_lowpc, Dwarf_Error * error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_lowpc(my_die, return_lowpc, error);		
		} // may allocate **error

		int die::highpc(Dwarf_Addr * return_highpc, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_highpc(my_die, return_highpc, error);
		} // may allocate **error

		int die::bytesize(Dwarf_Unsigned *return_size, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_bytesize(my_die, return_size, error);
		} // may allocate **error

		int die::bitsize(Dwarf_Unsigned *return_size, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_bitsize(my_die, return_size, error);
		} // may allocate **error

		int die::bitoffset(Dwarf_Unsigned *return_size, Dwarf_Error *error /*= 0*/)	
		{
			if (error == 0) error = p_last_error;
			return dwarf_bitoffset(my_die, return_size, error);
		} // may allocate **error

		int die::srclang(Dwarf_Unsigned *return_lang, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_srclang(my_die, return_lang, error);		
		}

		int die::arrayorder(Dwarf_Unsigned *return_order, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_arrayorder(my_die, return_order, error);
		}

		/*
		 * methods defined on global
		 */
		int global::get_name(char **return_name, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = a.p_last_error;
			return dwarf_globname(a.p_globals[i], return_name, error);		
		}	// TODO: string destructor

		int global::get_die_offset(Dwarf_Off *return_offset, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = a.p_last_error;
			return dwarf_global_die_offset(a.p_globals[i], return_offset, error);
		}	
		int global::get_cu_offset(Dwarf_Off *return_offset, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = a.p_last_error;	
			return dwarf_global_cu_offset(a.p_globals[i], return_offset, error);
		}
		int global::cu_die_offset_given_cu_header_offset(Dwarf_Off in_cu_header_offset,
			Dwarf_Off *out_cu_die_offset, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = a.p_last_error;	
			return dwarf_get_cu_die_offset_given_cu_header_offset(a.f.get_dbg(),
				in_cu_header_offset, out_cu_die_offset, error);
		}
		int global::name_offsets(char **return_name, Dwarf_Off *die_offset, 
			Dwarf_Off *cu_offset, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = a.p_last_error;
			return dwarf_global_name_offsets(a.p_globals[i], return_name,
				die_offset, cu_offset, error);
		}

		attribute::attribute(Dwarf_Half attr, attribute_array& a, Dwarf_Error *error /*= 0*/)
         : p_a(&a)
        {
			if (error == 0) error = p_a->p_last_error;
            
			Dwarf_Bool ret = false; 
            if (!(p_a->d.hasattr(attr, &ret), ret)) throw No_entry(); 
            
            for (int i = 0; i < a.cnt; i++)
            {
                Dwarf_Half out;
                if ((dwarf_whatattr(p_a->p_attrs[i], &out, error), out) == attr)
                {
                	this->i = i;
					this->p_raw_attr = p_a->p_attrs[i];
                    return;
                }
            }
            assert(false); // shouldn't happen, because we checked
        }

		/*
		 * methods defined on attribute
		 */
		int attribute::hasform(Dwarf_Half form, Dwarf_Bool *return_hasform,
			Dwarf_Error *error /*=0*/) const
		{
			if (error == 0) error = p_a->p_last_error;
			return dwarf_hasform(p_a->p_attrs[i], form, return_hasform, error);
		}
		int attribute::whatform(Dwarf_Half *return_form, Dwarf_Error *error /*=0*/) const
		{
			if (error == 0) error = p_a->p_last_error;
			return dwarf_whatform(p_a->p_attrs[i], return_form, error);
		}
		int attribute::whatform_direct(Dwarf_Half *return_form,	Dwarf_Error *error /*=0*/) const
		{
			if (error == 0) error = p_a->p_last_error;
			return dwarf_whatform_direct(p_a->p_attrs[i], return_form, error);
		}
		int attribute::whatattr(Dwarf_Half *return_attr, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = p_a->p_last_error;
			return dwarf_whatattr(p_a->p_attrs[i], return_attr, error);
		}
		int attribute::formref(Dwarf_Off *return_offset, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = p_a->p_last_error;
			return dwarf_formref(p_a->p_attrs[i], return_offset, error);
		}
		int attribute::formref_global(Dwarf_Off *return_offset, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = p_a->p_last_error;
			return dwarf_global_formref(p_a->p_attrs[i], return_offset, error);
		}
		int attribute::formaddr(Dwarf_Addr * return_addr, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = p_a->p_last_error;
			return dwarf_formaddr(p_a->p_attrs[i], return_addr, error);
		}
		int attribute::formflag(Dwarf_Bool * return_bool, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = p_a->p_last_error;
			return dwarf_formflag(p_a->p_attrs[i], return_bool, error);
		}
		int attribute::formudata(Dwarf_Unsigned * return_uvalue, Dwarf_Error * error /*=0*/) const
		{		
			if (error == 0) error = p_a->p_last_error;
			return dwarf_formudata(p_a->p_attrs[i], return_uvalue, error);
		}
		int attribute::formsdata(Dwarf_Signed * return_svalue, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = p_a->p_last_error;
			return dwarf_formsdata(p_a->p_attrs[i], return_svalue, error);
		}
		int attribute::formblock(Dwarf_Block ** return_block, Dwarf_Error * error /*=0*/) const
		{		
			if (error == 0) error = p_a->p_last_error; // TODO: fix this to be RAII
			return dwarf_formblock(p_a->p_attrs[i], return_block, error);
		}
		int attribute::formstring(char ** return_string, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = p_a->p_last_error; // TODO: fix this to be RAII
			return dwarf_formstring(p_a->p_attrs[i], return_string, error);
		}
		int attribute::loclist_n(Dwarf_Locdesc ***llbuf, Dwarf_Signed *listlen, Dwarf_Error *error /*=0*/) const
		{
			if (error == 0) error = p_a->p_last_error; // TODO: fix this to be RAII
			return dwarf_loclist_n(p_a->p_attrs[i], llbuf, listlen, error);
		}
		int attribute::loclist(Dwarf_Locdesc **llbuf, Dwarf_Signed *listlen, Dwarf_Error *error /*=0*/) const
		{
			if (error == 0) error = p_a->p_last_error; // TODO: fix this to be RAII
			return dwarf_loclist(p_a->p_attrs[i], llbuf, listlen, error);
		}
		
		/* methods defined on aranges */
		int aranges::get_info(int i, Dwarf_Addr *start, Dwarf_Unsigned *length, Dwarf_Off *cu_die_offset,
				Dwarf_Error *error/* = 0*/)
		{
			if (error == 0) error = p_last_error; // TODO: fix
			if (i >= cnt) throw No_entry();
			return dwarf_get_arange_info(p_aranges[i], start, length, cu_die_offset, error);
		}
		int aranges::get_info_for_addr(Dwarf_Addr addr, Dwarf_Addr *start, Dwarf_Unsigned *length, 
			Dwarf_Off *cu_die_offset, Dwarf_Error *error/* = 0*/)
		{
			if (error == 0) error = p_last_error; // TODO: fix
			cerr << "Getting info for addr 0x" << std::hex << addr << std::dec
				<< " from aranges block at " << p_aranges << endl;
			for (int j = 0; j < cnt && j < 10; ++j)
			{
				Dwarf_Addr tmp_start;
				Dwarf_Unsigned tmp_length;
				Dwarf_Off tmp_cu_die_offset;
				int ret2 = dwarf_get_arange_info(p_aranges[j], 
					&tmp_start, &tmp_length, &tmp_cu_die_offset, error);
				assert(ret2 == DW_DLV_OK);
				cerr << "Arange number " << j << " has "
					<< "start address 0x" << std::hex << tmp_start << std::dec
					<< ", length " << tmp_length
					<< ", CU offset 0x" << std::hex << tmp_cu_die_offset << std::dec << endl;
			}
			if (cnt > 10) cerr << "More aranges follow." << endl;
			Dwarf_Arange returned;
			assert(p_aranges);
			assert(cnt > 0);
			int ret = dwarf_get_arange(p_aranges, cnt, addr, &returned, error);
			if (ret == DW_DLV_OK)
			{
				cerr << "get_arange succeeded" << endl;
				int ret2 = dwarf_get_arange_info(returned, start, length, cu_die_offset, error);
				return ret2;
			} else if (ret == -1) cerr << "get_arange found no entry convering address 0x" 
				<< std::hex << addr << std::dec << endl;
			else cerr << "get_arange failed with error " << ret << endl;
			return ret;
			
		}

		
		
		std::ostream& operator<<(std::ostream& s, const Dwarf_Locdesc& ld)
		{
			s << dwarf::encap::loc_expr(ld);
			return s;
		}	
		std::ostream& operator<<(std::ostream& s, const Dwarf_Loc& l)
		{
        	// HACK: we can't infer the DWARF standard from the Dwarf_Loc we've been passed,
            // so use the default.
			s << "0x" << std::hex << l.lr_offset << std::dec
				<< ": " << dwarf::spec::DEFAULT_DWARF_SPEC.op_lookup(l.lr_atom);
			std::ostringstream buf;
			std::string to_append;
           
			switch (dwarf::spec::DEFAULT_DWARF_SPEC.op_operand_count(l.lr_atom))
			{
				case 2:
					buf << ", " << dwarf::encap::attribute_value(
						l.lr_number2, 
						dwarf::spec::DEFAULT_DWARF_SPEC.op_operand_form_list(l.lr_atom)[1]
					);
					to_append += buf.str();
				case 1:
					buf.clear();
					buf << "(" << dwarf::encap::attribute_value(
						l.lr_number, 
						dwarf::spec::DEFAULT_DWARF_SPEC.op_operand_form_list(l.lr_atom)[0]
					);
					to_append.insert(0, buf.str());
					to_append += ")";
				case 0:
					s << to_append;
					break;
				default: s << "(unexpected number of operands) ";
			}
			s << ";";
			return s;			
		}
		std::ostream& operator<<(std::ostream& s, const loclist& ll)
		{
			s << "(loclist) {";
			for (int i = 0; i < ll.len(); i++)
			{
				if (i > 0) s << ", ";
				s << ll[i];
			}
			s << "}";
			return s;
		}
	
		bool operator<(const dwarf::lib::Dwarf_Loc& arg1, const dwarf::lib::Dwarf_Loc& arg2)
		{
			return arg1.lr_atom <  arg2.lr_atom
			||     (arg1.lr_atom == arg2.lr_atom && arg1.lr_number <  arg2.lr_number)
			||     (arg1.lr_atom == arg2.lr_atom && arg1.lr_number ==  arg2.lr_number && arg1.lr_number2 <  arg2.lr_number2)
			||     (arg1.lr_atom == arg2.lr_atom && arg1.lr_number ==  arg2.lr_number && arg1.lr_number2 == arg2.lr_number2 && 
				arg1.lr_offset < arg2.lr_offset); 
		}
		
		void evaluator::eval()
		{
			//std::vector<Dwarf_Loc>::iterator i = expr.begin();
            if (i != expr.end() && i != expr.begin())
            {
            	/* This happens when we stopped at a DW_OP_piece argument. 
                 * Advance the opcode iterator and clear the stack. */
                ++i;
                while (!m_stack.empty()) m_stack.pop();
			}
            boost::optional<std::string> error_detail;
			while (i != expr.end())
			{
				// FIXME: be more descriminate -- do we want to propagate valueness? probably not
				tos_is_value = false;
				switch(i->lr_atom)
				{
					case DW_OP_const1u:
					case DW_OP_const2u:
					case DW_OP_const4u:
					case DW_OP_const8u:
					case DW_OP_constu:
						m_stack.push(i->lr_number);
						break;
 					case DW_OP_const1s:
					case DW_OP_const2s:
					case DW_OP_const4s:
					case DW_OP_const8s:
					case DW_OP_consts:
						m_stack.push((Dwarf_Signed) i->lr_number);
						break;
                   case DW_OP_plus_uconst: {
                    	int tos = m_stack.top();
                        m_stack.pop();
                        m_stack.push(tos + i->lr_number);
                    } break;
					case DW_OP_plus: {
						int arg1 = m_stack.top(); m_stack.pop();
						int arg2 = m_stack.top(); m_stack.pop();
						m_stack.push(arg1 + arg2);
					} break;
                    case DW_OP_fbreg: {
                    	if (!frame_base) goto logic_error;
                        m_stack.push(*frame_base + i->lr_number);
                    } break;
                    case DW_OP_call_frame_cfa: {
                    	if (!frame_base) goto logic_error;
                        m_stack.push(*frame_base);
                    } break;
                    case DW_OP_piece: {
                    	/* Here we do something special: leave the opcode iterator
                         * pointing at the piece argument, and return. This allow us
                         * to probe the piece size (by getting *i) and to resume by
                         * calling eval() again. */
                         ++i;
                    }    return;
                    case DW_OP_breg0:
                    case DW_OP_breg1:
                    case DW_OP_breg2:
                    case DW_OP_breg3:
                    case DW_OP_breg4:
                    case DW_OP_breg5:
                    case DW_OP_breg6:
                    case DW_OP_breg7:
                    case DW_OP_breg8:
                    case DW_OP_breg9:
                    case DW_OP_breg10:
                    case DW_OP_breg11:
                    case DW_OP_breg12:
                    case DW_OP_breg13:
                    case DW_OP_breg14:
                    case DW_OP_breg15:
                    case DW_OP_breg16:
                    case DW_OP_breg17:
                    case DW_OP_breg18:
                    case DW_OP_breg19:
                    case DW_OP_breg20:
                    case DW_OP_breg21:
                    case DW_OP_breg22:
                    case DW_OP_breg23:
                    case DW_OP_breg24:
                    case DW_OP_breg25:
                    case DW_OP_breg26:
                    case DW_OP_breg27:
                    case DW_OP_breg28:
                    case DW_OP_breg29:
                    case DW_OP_breg30:
                    case DW_OP_breg31:
                    {
						/* the breg family get the contents of a register and add an offset */ 
                    	if (!p_regs) goto no_regs;
                    	int regnum = i->lr_atom - DW_OP_breg0;
                        m_stack.push(p_regs->get(regnum) + i->lr_number);
                    } break;
                    case DW_OP_addr:
                    {
                    	m_stack.push(i->lr_number);
                    } break;
					case DW_OP_reg0:
					case DW_OP_reg1:
					case DW_OP_reg2:
					case DW_OP_reg3:
					case DW_OP_reg4:
					case DW_OP_reg5:
					case DW_OP_reg6:
					case DW_OP_reg7:
					case DW_OP_reg8:
					case DW_OP_reg9:
					case DW_OP_reg10:
					case DW_OP_reg11:
					case DW_OP_reg12:
					case DW_OP_reg13:
					case DW_OP_reg14:
					case DW_OP_reg15:
					case DW_OP_reg16:
					case DW_OP_reg17:
					case DW_OP_reg18:
					case DW_OP_reg19:
					case DW_OP_reg20:
					case DW_OP_reg21:
					case DW_OP_reg22:
					case DW_OP_reg23:
					case DW_OP_reg24:
					case DW_OP_reg25:
					case DW_OP_reg26:
					case DW_OP_reg27:
					case DW_OP_reg28:
					case DW_OP_reg29:
					case DW_OP_reg30:
					case DW_OP_reg31:
					{
						/* the reg family just get the contents of the register */
						if (!p_regs) goto no_regs;
						int regnum = i->lr_atom - DW_OP_reg0;
						m_stack.push(p_regs->get(regnum));
					} break;
					case DW_OP_lit0:
					case DW_OP_lit1:
					case DW_OP_lit2:
					case DW_OP_lit3:
					case DW_OP_lit4:
					case DW_OP_lit5:
					case DW_OP_lit6:
					case DW_OP_lit7:
					case DW_OP_lit8:
					case DW_OP_lit9:
					case DW_OP_lit10:
					case DW_OP_lit11:
					case DW_OP_lit12:
					case DW_OP_lit13:
					case DW_OP_lit14:
					case DW_OP_lit15:
					case DW_OP_lit16:
					case DW_OP_lit17:
					case DW_OP_lit18:
					case DW_OP_lit19:
					case DW_OP_lit20:
					case DW_OP_lit21:
					case DW_OP_lit22:
					case DW_OP_lit23:
					case DW_OP_lit24:
					case DW_OP_lit25:
					case DW_OP_lit26:
					case DW_OP_lit27:
					case DW_OP_lit28:
					case DW_OP_lit29:
					case DW_OP_lit30:
					case DW_OP_lit31:
						m_stack.push(i->lr_atom - DW_OP_lit0);
						break;
					case DW_OP_stack_value:
						/* This means that the object has no address, but that the 
						 * DWARF evaluator has just computed its *value*. We record
						 * this. */
						tos_is_value = true;
						break;
					case DW_OP_deref_size:
					case DW_OP_deref:
						/* FIXME: we can do this one if we have p_mem analogous to p_regs. */
						throw No_entry();
					default:
						std::cerr << "Error: unrecognised opcode: " << spec.op_lookup(i->lr_atom) << std::endl;
						throw Not_supported("unrecognised opcode");
					no_regs:
						std::cerr << "Warning: asked to evaluate register-dependent expression with no registers." << std::endl;
						throw No_entry();
					logic_error:
						std::cerr << "Logic error in DWARF expression evaluator";
						if (error_detail) std::cerr << ": " << *error_detail;
						std::cerr << std::endl;
						assert(false);
						throw Not_supported(error_detail ? *error_detail : "unknown");
				}
			i++;
			}
		}
		Dwarf_Unsigned eval(const encap::loclist& loclist,
			Dwarf_Addr vaddr,
			Dwarf_Signed frame_base,
			boost::optional<regs&> regs,
			const ::dwarf::spec::abstract_def& spec,
			const std::stack<Dwarf_Unsigned>& initial_stack)
		{
			assert(false); return 0UL;
		}
		
		bool operator==(const /*dwarf::encap::expr_instr*/Dwarf_Loc& e1, 
			const /*dwarf::encap::expr_instr*/Dwarf_Loc& e2)
		{
			return e1.lr_atom == e2.lr_atom
				&& e1.lr_number == e2.lr_number
				&& e1.lr_number2 == e2.lr_number2
				&& e1.lr_offset == e2.lr_offset;
		}
		bool operator!=(const /*dwarf::encap::expr_instr*/Dwarf_Loc& e1,
			const /*dwarf::encap::expr_instr*/Dwarf_Loc& e2)
		{
			return !(e1 == e2);
		}
		bool operator==(const /*dwarf::encap::expr_instr*/Dwarf_Ranges& e1, 
			const /*dwarf::encap::expr_instr*/Dwarf_Ranges& e2)
		{
			return e1.dwr_addr1 == e2.dwr_addr1
				&& e1.dwr_addr2 == e2.dwr_addr2
				&& e1.dwr_type == e2.dwr_type;
		}
		bool operator!=(const /*dwarf::encap::expr_instr*/Dwarf_Ranges& e1,
			const /*dwarf::encap::expr_instr*/Dwarf_Ranges& e2)
		{
			return !(e1 == e2);
		}

	} // end namespace lib
	
	namespace core
	{
/* from type_die */
		size_t type_hash_fn(iterator_df<type_die> t) 
		{
			opt<uint32_t> summary = t ? t->summary_code() : opt<uint32_t>(0);
			return summary ? *summary : 0;
		}
		bool type_eq_fn(iterator_df<type_die> t1, iterator_df<type_die> t2)
		{
			return (!t1 && !t2) || (t1 && t2 && *t1 == *t2);
		}
		void walk_type(iterator_df<type_die> t, iterator_df<program_element_die> reason, 
			const std::function<bool(iterator_df<type_die>, iterator_df<program_element_die>)>& pre_f, 
			const std::function<void(iterator_df<type_die>, iterator_df<program_element_die>)>& post_f,
			const type_set& currently_walking /* = empty */)
		{
			if (currently_walking.find(t) != currently_walking.end()) return; // "grey node"
			
			bool continue_recursing;
			if (pre_f) continue_recursing = pre_f(t, reason); // i.e. we do walk "void"
			else continue_recursing = true;
			
			type_set next_currently_walking = currently_walking; 
			next_currently_walking.insert(t);
			
			if (continue_recursing)
			{

				if (!t) { /* void case; just post-visit */ }
				else if (t.is_a<type_chain_die>()) // unary case -- includes typedefs, arrays, pointer/reference, ...
				{
					// recursively walk the chain's target
					walk_type(t.as_a<type_chain_die>()->find_type(), t, pre_f, post_f, next_currently_walking);
				}
				else if (t.is_a<with_data_members_die>()) 
				{
					// recursively walk all members
					auto member_children = t.as_a<with_data_members_die>().children().subseq_of<member_die>();
					for (auto i_child = member_children.first;
						i_child != member_children.second; ++i_child)
					{
						walk_type(i_child->find_type(), i_child.base().base(), pre_f, post_f, next_currently_walking);
					}
					// visit all inheritances
					auto inheritance_children = t.as_a<with_data_members_die>().children().subseq_of<inheritance_die>();
					for (auto i_child = inheritance_children.first;
						i_child != inheritance_children.second; ++i_child)
					{
						walk_type(i_child->find_type(), i_child.base().base(), pre_f, post_f, next_currently_walking);
					}
				}
				else if (t.is_a<subrange_type_die>())
				{
					// visit the base type
					auto explicit_t = t.as_a<subrange_type_die>()->find_type();
					// HACK: assume this is the same as for enums
					walk_type(explicit_t ? explicit_t : t.enclosing_cu()->implicit_enum_base_type(), t, pre_f, post_f, next_currently_walking);
				}
				else if (t.is_a<enumeration_type_die>())
				{
					// visit the base type -- HACK: assume subrange base is same as enum's
					auto explicit_t = t.as_a<enumeration_type_die>()->find_type();
					walk_type(explicit_t ? explicit_t : t.enclosing_cu()->implicit_enum_base_type(), t, pre_f, post_f, next_currently_walking);
				}
				else if (t.is_a<type_describing_subprogram_die>())
				{
					auto sub_t = t.as_a<type_describing_subprogram_die>();
					walk_type(sub_t->find_type(), sub_t, pre_f, post_f);
					auto fps = sub_t.children().subseq_of<formal_parameter_die>();
					for (auto i_fp = fps.first; i_fp != fps.second; ++i_fp)
					{
						walk_type(i_fp->find_type(), i_fp.base().base(), pre_f, post_f, next_currently_walking);
					}
				}
				else
				{
					// what are our nullary cases?
					assert(t.is_a<base_type_die>() || t.is_a<unspecified_type_die>());
				}
			} // end if continue_recursing

			if (post_f) post_f(t, reason);
		}
		/* begin pasted from adt.cpp */
		opt<Dwarf_Unsigned> type_die::calculate_byte_size(optional_root_arg_decl) const
		{
			return get_byte_size(opt_r); 
		}
		iterator_df<type_die> type_die::get_concrete_type(optional_root_arg_decl) const
		{
			// by default, our concrete self is our self -- we have to find ourselves. :-(
			return get_root(opt_r).find(get_offset());
		}
		iterator_df<type_die> type_die::get_unqualified_type(optional_root_arg_decl) const
		{
			// by default, our unqualified self is our self -- we have to find ourselves. :-(
			return get_root(opt_r).find(get_offset());
		}
		
		opt<uint32_t> type_die::summary_code(optional_root_arg_decl) const
		{
			/* if we have it cached, return that */
			if (cached_summary_code) return *cached_summary_code;
		
			/* FIXME: factor this into the various subclass cases. */
			// we have to find ourselves. :-(
			auto t = get_root(opt_r).find(get_offset()).as_a<type_die>();
			
			auto name_for_type_die = [](core::iterator_df<core::type_die> t) -> opt<string> {
				if (t.is_a<dwarf::core::subprogram_die>())
				{
					/* When interpreted as types, subprograms don't have names. */
					return opt<string>();
				}
				else return *t.name_here();
			};
			
			auto type_summary_code = [](core::iterator_df<core::type_die> t) -> opt<uint32_t> {
				if (!t) return opt<uint32_t>(0);
				else return t->summary_code();
			};
			
			/* Here we compute a 4-byte hash-esque summary of a data type's 
			 * definition. The intentions here are that 
			 *
			 * binary-incompatible definitions of two types will always
			   compare different, even if the incompatibility occurs 

			   - in compiler decisions (e.g. bitfield positions, pointer
			     encoding, padding, etc..)

			   - in a child (nested) object.

			 * structurally distinct definitions will always compare different, 
			   even if at the leaf level, they are physically compatible.

			 * binary compatible, structurally compatible definitions will compare 
			   alike iff they are nominally identical at the top-level. It doesn't
			   matter if field names differ. HMM: so what about nested structures' 
			   type names? Answer: not sure yet, but easiest is to require that they
			   match, so our implementation can just use recursion.

			 * WHAT about prefixes? e.g. I define struct FILE with some padding, 
			   and you define it with some implementation-private fields? We handle
			   this at the libcrunch level; here we just want to record that there
			   are two different definitions out there.

			 *
			 * Consequences: 
			 * 
			 * - encode all base type properties
			 * - encode pointer encoding
			 * - encode byte- and bit-offsets of every field
			 */
			using lib::Dwarf_Unsigned;
			using lib::Dwarf_Half;
			using namespace dwarf::core;

			if (!t)
			{
				// we got void
				return opt<uint32_t>(0);
			}

			auto concrete_t = t->get_concrete_type();
			if (!concrete_t)
			{
				// we got a typedef of void
				return opt<uint32_t>(0);
			}

			/* For declarations, if we can't find their definition, we return opt<>(). */
			if (concrete_t->get_declaration() && *concrete_t->get_declaration())
			{
				iterator_df<> found = concrete_t->find_definition();
				concrete_t = found.as_a<type_die>();
				if (!concrete_t) return opt<uint32_t>();
			}

			summary_code_word_t output_word;
			assert(output_word.val);
			Dwarf_Half tag = concrete_t.tag_here();
			if (concrete_t.is_a<base_type_die>())
			{
				auto base_t = concrete_t.as_a<core::base_type_die>();
				unsigned encoding = base_t->get_encoding();
				assert(base_t->get_byte_size());
				unsigned byte_size = *base_t->get_byte_size();
				unsigned bit_size = base_t->get_bit_size() ? *base_t->get_bit_size() : byte_size * 8;
				unsigned bit_offset = base_t->get_bit_offset() ? *base_t->get_bit_offset() : 0;
				output_word << DW_TAG_base_type << encoding << byte_size << bit_size << bit_offset;
			} 
			else if (concrete_t.is_a<enumeration_type_die>())
			{
				// shift in the enumeration name
				if (concrete_t.name_here())
				{
					output_word << *name_for_type_die(concrete_t);
				} else output_word << concrete_t.offset_here();

				// shift in the names and values of each enumerator
				auto enum_t = concrete_t.as_a<enumeration_type_die>();
				auto enumerators = enum_t.children().subseq_of<enumerator_die>();
				int last_enum_value = -1;
				for (auto i_enum = enumerators.first; i_enum != enumerators.second; ++i_enum)
				{
					output_word << *i_enum->get_name();
					if (i_enum->get_const_value())
					{
						last_enum_value = *i_enum->get_const_value();
						output_word << last_enum_value;
					} else output_word << last_enum_value++;
				}

				// then shift in the base type's summary code
				if (!enum_t->get_type())
				{
					// cerr << "Warning: saw enum with no type" << endl;
					auto implicit_t = enum_t.enclosing_cu()->implicit_enum_base_type();
					if (!implicit_t)
					{
						cerr << "Warning: saw enum with no type" << endl;
					} else output_word << type_summary_code(implicit_t);
				}
				else
				{
					output_word << type_summary_code(enum_t->get_type());
				}
			} 
			else if (concrete_t.is_a<subrange_type_die>())
			{
				auto subrange_t = concrete_t.as_a<subrange_type_die>();

				// shift in the name, if any
				if (concrete_t.name_here())
				{
					output_word << *name_for_type_die(concrete_t);
				} else output_word << concrete_t.offset_here();

				// then shift in the base type's summary code
				if (!subrange_t->get_type())
				{
					cerr << "Warning: saw subrange with no type" << endl;
				}
				else
				{
					output_word << type_summary_code(subrange_t->get_type());
				}

				/* Then shift in the upper bound and lower bound, if present
				 * NOTE: this means unnamed boundless subrange types have the 
				 * same code as their underlying type. This is probably what we want. */
				if (subrange_t->get_upper_bound())
				{
					output_word << *subrange_t->get_upper_bound();
				}
				if (subrange_t->get_lower_bound())
				{
					output_word << *subrange_t->get_lower_bound();
				}
			} 
			else if (concrete_t.is_a<type_describing_subprogram_die>())
			{
				auto subp_t = concrete_t.as_a<type_describing_subprogram_die>();
				
				// shift in the argument and return types
				auto return_type = subp_t->get_return_type();
				output_word << type_summary_code(return_type);

				// shift in something to distinguish void(void) from void
				output_word << "()";

				auto fps = concrete_t.children().subseq_of<formal_parameter_die>();
				for (auto i_fp = fps.first; i_fp != fps.second; ++i_fp)
				{
					output_word << type_summary_code(i_fp->find_type());
				}

				if (subp_t->is_variadic())
				{
					output_word << "...";
				}
			}
			else if (concrete_t.is_a<address_holding_type_die>())
			{
				/* NOTE: actually, we *do* want to pay attention to what the pointer points to, 
				 * i.e. its contract. BUT there's a problem: recursive data types! For now, we
				 * use a giant HACK: if we're a pointer-to-member, use only the name. */
				auto ptr_t = concrete_t.as_a<core::address_holding_type_die>();
				unsigned ptr_size = *ptr_t->calculate_byte_size();
				unsigned addr_class = ptr_t->get_address_class() ? *ptr_t->get_address_class() : 0;
				if (addr_class != 0)
				{
					switch(addr_class) 
					{
						default:
							assert(false); // nobody seems to use this feature so far
						/* NOTE: There is also something called DWARF Pointer-Encoding (PEs).
						   This is a DWARF representation issue, used in frame info, and is not 
						   something we care about. */
					}
				}
				auto target_t = ptr_t->get_type();
				if (target_t.is_real_die_position()) target_t = target_t->get_concrete_type();
				opt<uint32_t> target_code;
				if (target_t.is_real_die_position() && target_t.is_a<with_data_members_die>())
				{
					summary_code_word_t tmp_output_word;
					// add in the name only
					if (target_t.name_here())
					{
						tmp_output_word << *name_for_type_die(target_t);
					} else tmp_output_word << target_t.offset_here();

					target_code = *tmp_output_word.val;
				} else target_code = type_summary_code(target_t);
				output_word << tag << ptr_size << addr_class << target_code;
			}
			else if (concrete_t.is_a<with_data_members_die>())
			{
				// add in the name
				if (concrete_t.name_here())
				{
					output_word << *name_for_type_die(concrete_t);
				} else output_word << concrete_t.offset_here();

				// for each member 
				auto members = concrete_t.children().subseq_of<core::with_dynamic_location_die>();
				for (auto i_member = members.first; i_member != members.second; ++i_member)
				{
					// skip members that are mere declarations 
					if (i_member->get_declaration() && *i_member->get_declaration()) continue;

					// calculate its offset
					opt<Dwarf_Unsigned> opt_offset = i_member->byte_offset_in_enclosing_type();
					if (!opt_offset)
					{
						cerr << "Warning: saw member " << *i_member << " with no apparent offset." << endl;
						continue;
					}
					auto member_type = i_member->get_type();
					assert(member_type);
					assert(member_type.is_a<type_die>());

					output_word << (opt_offset ? *opt_offset : 0);
					// FIXME: also its bit offset!

					output_word << type_summary_code(member_type);
				}
			}
			else if (concrete_t.is_a<array_type_die>())
			{
				// if we're a member of something, we should be bounded in all dimensions
				auto opt_el_type = concrete_t.as_a<array_type_die>()->ultimate_element_type();
				auto opt_el_count = concrete_t.as_a<array_type_die>()->ultimate_element_count();
				output_word << (opt_el_type ? type_summary_code(opt_el_type) : opt<uint32_t>())
					<< (opt_el_count ? *opt_el_count : 0);
					// FIXME: also the factoring into dimensions needs to be taken into account
			}
			else if (concrete_t.is_a<unspecified_type_die>())
			{
				cerr << "Warning: saw unspecified type " << concrete_t;
				output_word.val = opt<uint32_t>();
			}
			else 
			{
				cerr << "Warning: didn't understand type " << concrete_t;
			}

			// pointer-to-incomplete, etc., will still give us incomplete answer
			assert (!concrete_t || !(output_word.val) || *output_word.val != 0);

			this->cached_summary_code = output_word.val; 
			
			return output_word.val;
			// return std::numeric_limits<uint32_t>::max();
			
		}
		bool type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, optional_root_arg_decl) const
		{
			if (!t) return false;
			
			cerr << "Testing type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			return t.tag_here() == get_tag(); // will be refined in subclasses
		}
		bool type_die::equal(iterator_df<type_die> t, 
			const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, 
			optional_root_arg_decl) const
		{
			set<pair< iterator_df<type_die>, iterator_df<type_die> > > flipped_set;
			auto& r = get_root(opt_r);
			auto self = r.find(get_offset());
			
			// iterator equality always implies type equality
			if (self == t) return true;
			
			if (assuming_equal.find(make_pair(self, t)) != assuming_equal.end())
			{
				return true;
			}
			/* If the two iterators share a root, check the cache */
			if (t && &t.root() == &self.root())
			{
				auto found = self.root().equal_to.find(t.offset_here());
				if (found != self.root().equal_to.end())
				{
					return found->second.second;
				}
			}
			// we have to find t
			bool ret;
			bool t_may_equal_self;
			bool self_may_equal_t = this->may_equal(t, assuming_equal);
			if (!self_may_equal_t) { ret = false; goto out; }
			
			// we need to flip our set of pairs
			for (auto i_pair = assuming_equal.begin(); i_pair != assuming_equal.end(); ++i_pair)
			{
				flipped_set.insert(make_pair(i_pair->second, i_pair->first));
			}
			
			t_may_equal_self =  // we have to find ourselves :-(
				   // ... using our constructing root! :-((((((
				t->may_equal(self, flipped_set, r);
			if (!t_may_equal_self) { ret = false; goto out; }
			ret = true;
			// if we're unequal then we should not be the same DIE (equality is reflexive)
		out:
			/* If we're returning false, we'd better not be the same DIE. */
			assert(ret || !t || 
				!(&t.get_root() == &self.get_root() && t.offset_here() == self.offset_here()));
			/* If the two iterators share a root, cache the result */
			if (t && &t.root() == &self.root())
			{
				self.root().equal_to.insert(make_pair(self.offset_here(), make_pair(t.offset_here(), ret)));
				self.root().equal_to.insert(make_pair(t.offset_here(), make_pair(self.offset_here(), ret)));
			}
			
			return ret;
		}
		bool type_die::operator==(const dwarf::core::type_die& t) const
		{ return equal(get_root(opt<root_die&>()).find(t.get_offset()), {}); }
/* from base_type_die */
		bool base_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, optional_root_arg_decl) const
		{
			if (!t) return false;
			
			if (get_tag() != t.tag_here()) return false;

			if (get_name() != t.name_here()) return false;

			auto other_base_t = t.as_a<base_type_die>();
			
			bool encoding_equal = get_encoding() == other_base_t->get_encoding();
			if (!encoding_equal) return false;
			
			bool byte_size_equal = get_byte_size() == other_base_t->get_byte_size();
			if (!byte_size_equal) return false;
			
			bool bit_size_equal =
			// presence equal
				(!get_bit_size() == !other_base_t->get_bit_size())
			// and if we have one, it's equal
			&& (!get_bit_size() || *get_bit_size() == *other_base_t->get_bit_size());
			if (!bit_size_equal) return false;
			
			bool bit_offset_equal = 
			// presence equal
				(!get_bit_offset() == !other_base_t->get_bit_offset())
			// and if we have one, it's equal
			&& (!get_bit_offset() || *get_bit_offset() == *other_base_t->get_bit_offset());
			if (!bit_offset_equal) return false;
			
			return true;
		}
/* from array_type_die */
		iterator_df<type_die> array_type_die::get_concrete_type(optional_root_arg_decl) const
		{
			// for arrays, our concrete self is our self -- we have to find ourselves. :-(
			return get_root(opt_r).find(get_offset());
		}
		bool array_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, optional_root_arg_decl) const
		{
			if (!t) return false;
			
			cerr << "Testing array_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;

			if (get_name() != t.name_here()) return false;

			// our subrange type(s) should be equal, if we have them
			auto our_subr_children = children().subseq_of<subrange_type_die>();
			auto their_subr_children = t->children().subseq_of<subrange_type_die>();
			auto i_theirs = their_subr_children.first;
			for (auto i_subr = our_subr_children.first; i_subr != our_subr_children.second;
				++i_subr, ++i_theirs)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_subr_children.second) return false;
				
				bool types_equal = 
				// presence equal
					(!i_subr->get_type() == !i_subr->get_type())
				// and if we have one, it's equal to theirs
				&& (!i_subr->get_type() || i_subr->get_type()->equal(i_theirs->get_type(), assuming_equal));
				
				if (!types_equal) return false;
			}
			// if they had more, we're unequal
			if (i_theirs != their_subr_children.second) return false;
			
			// our element type(s) should be equal
			bool types_equal = get_type()->equal(t.as_a<array_type_die>()->get_type(), assuming_equal);
			if (!types_equal) return false;
			
			return true;
		}
/* from subrange_type_die */
		bool subrange_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, optional_root_arg_decl) const
		{
			if (!t) return false;
			cerr << "Testing subrange_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			
			if (get_name() != t.name_here()) return false;

			auto subr_t = t.as_a<subrange_type_die>();
			
			// our base type(s) should be equal
			bool types_equal = 
			// presence equal
				(!get_type() == !subr_t->get_type())
			// if we have one, it should equal theirs
			&& (!get_type() || get_type()->equal(subr_t->get_type(), assuming_equal));
			if (!types_equal) return false;
			
			// our upper bound and lower bound should be equal
			bool lower_bound_equal = get_lower_bound() == subr_t->get_lower_bound();
			if (!lower_bound_equal) return false;
			
			bool upper_bound_equal = get_upper_bound() == subr_t->get_upper_bound();
			if (!upper_bound_equal) return false;
			
			bool count_equal = get_count() == subr_t->get_count();
			if (!count_equal) return false;
			
			return true;
		}
/* from enumeration_type_die */
		bool enumeration_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, optional_root_arg_decl) const
		{
			if (!t) return false;
			cerr << "Testing enumeration_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			
			if (get_name() != t.name_here()) return false;
		
			auto enum_t = t.as_a<enumeration_type_die>();
			
			// our base type(s) should be equal
			bool types_equal = 
			// presence equal
				(!get_type() == !enum_t->get_type())
			// if we have one, it should equal theirs
			&& (!get_type() || get_type()->equal(enum_t->get_type(), assuming_equal));
			if (!types_equal) return false;

			/* We need like-named, like-valued enumerators. */
			auto our_enumerator_children = children().subseq_of<enumerator_die>();
			auto their_enumerator_children = t->children().subseq_of<enumerator_die>();
			auto i_theirs = their_enumerator_children.first;
			for (auto i_memb = our_enumerator_children.first; i_memb != our_enumerator_children.second;
				++i_memb, ++i_theirs)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_enumerator_children.second) return false;

				if (i_memb->get_name() != i_theirs->get_name()) return false;
				
				if (i_memb->get_const_value() != i_theirs->get_const_value()) return false;
			}
			if (i_theirs != their_enumerator_children.second) return false;
			
			return true;
		}
/* from qualified_type_die */
		iterator_df<type_die> qualified_type_die::get_unqualified_type(optional_root_arg_decl) const
		{
			// for qualified types, our unqualified self is our get_type, recursively unqualified
			opt<iterator_df<type_die> > opt_next_type = get_type(opt_r);
			if (!opt_next_type) return iterator_base::END; 
			if (!opt_next_type.is_a<qualified_type_die>()) return opt_next_type;
			else return iterator_df<qualified_type_die>(std::move(opt_next_type))
				->get_unqualified_type();
		} 
/* from spec::type_chain_die */
		opt<Dwarf_Unsigned> type_chain_die::calculate_byte_size(optional_root_arg_decl) const
		{
			// Size of a type_chain is always the size of its concrete type
			// which is *not* to be confused with its pointed-to type!
			auto next_type = get_concrete_type(opt_r);
			if (get_offset() == next_type.offset_here())
			{
				assert(false); // we're too generic to know our byte size; should have hit a different overload
			}
			else if (next_type != iterator_base::END)
			{
				auto to_return = next_type->calculate_byte_size(opt_r);
				if (!to_return)
				{
					cerr << "Type chain concrete type " << *get_concrete_type(opt_r)
						<< " returned no byte size" << endl;
				}
				return to_return;
			}
			else
			{
				cerr << "Type with no concrete type: " << *this << endl;
				return opt<Dwarf_Unsigned>();
			}
		}
		bool type_chain_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, optional_root_arg_decl) const
		{
			cerr << "Testing type_chain_die::may_equal() (default case)" << endl;
			
			return get_tag() == t.tag_here() && 
				(
					(!get_type() && !t.as_a<type_chain_die>()->get_type())
				||  (get_type() && t.as_a<type_chain_die>()->get_type() && 
					get_type()->equal(t.as_a<type_chain_die>()->get_type(), assuming_equal)));
		}
		iterator_df<type_die> type_chain_die::get_concrete_type(optional_root_arg_decl) const
		{
			// pointer and reference *must* override us -- they do not follow chain
			assert(get_tag() != DW_TAG_pointer_type
				&& get_tag() != DW_TAG_reference_type
				&& get_tag() != DW_TAG_rvalue_reference_type
				&& get_tag() != DW_TAG_array_type);
			
			root_die& r = get_root(opt_r); 
			auto opt_next_type = get_type(r);
			if (!opt_next_type) return iterator_base::END; // a.k.a. None
			if (!get_spec(r).tag_is_type(opt_next_type.tag_here()))
			{
				cerr << "Warning: following type chain found non-type " << opt_next_type << endl;
				// find ourselves :-(
				return r.find(get_offset());
			} 
			else return opt_next_type->get_concrete_type(opt_r);
		}
/* from spec::address_holding_type_die */  
		iterator_df<type_die> address_holding_type_die::get_concrete_type(optional_root_arg_decl) const 
		{
			// we have to find ourselves :-(
			return get_root(opt_r).find(get_offset());
		}
		opt<Dwarf_Unsigned> address_holding_type_die::calculate_byte_size(optional_root_arg_decl) const 
		{
			root_die& r = get_root(opt_r);
			auto opt_size = get_byte_size(r);
			if (opt_size) return opt_size;
			else return r.cu_pos(get_enclosing_cu_offset())->get_address_size(/*opt_r*/);
		}
/* from spec::array_type_die */
		opt<Dwarf_Unsigned> array_type_die::element_count(optional_root_arg_decl) const
		{
			auto element_type = get_type(opt_r);
			assert(element_type != iterator_base::END);
			opt<Dwarf_Unsigned> count;
			root_die& r = get_root(opt_r);
			// we have to find ourselves. :-(
			auto self = r.find(get_offset());
			assert(self != iterator_base::END);
			auto enclosing_cu = r.cu_pos(get_enclosing_cu_offset());
			auto opt_implicit_lower_bound = enclosing_cu->implicit_array_base();
			
			auto subrs = self.children_here().subseq_of<subrange_type_die>();
			for (auto i_subr = std::move(subrs.first); i_subr != subrs.second; ++i_subr)
			{
				auto opt_count = i_subr->get_count(r);
				if (opt_count) 
				{
					count = *opt_count;
					break;
				}
				else
				{
					auto opt_lower_bound = i_subr->get_lower_bound(r);
					Dwarf_Unsigned lower_bound;
					if (opt_lower_bound) lower_bound = *opt_lower_bound;
					else if (opt_implicit_lower_bound) lower_bound = *opt_implicit_lower_bound;
					else break; // give up
					
					opt<Dwarf_Unsigned> opt_upper_bound = i_subr->get_upper_bound(r);
					if (!opt_upper_bound) break; // give up
					
					Dwarf_Unsigned upper_bound = *opt_upper_bound;
					assert(upper_bound < 100000000); // detects most garbage
					count = upper_bound - lower_bound + 1;
					break;
				}
			}
			return count;
		}

		opt<Dwarf_Unsigned> array_type_die::calculate_byte_size(optional_root_arg_decl) const
		{
			auto element_type = get_type(opt_r);
			assert(element_type != iterator_base::END);
			opt<Dwarf_Unsigned> count = element_count(opt_r);
			opt<Dwarf_Unsigned> calculated_byte_size = element_type->calculate_byte_size(opt_r);
			if (count && calculated_byte_size) return *count * *calculated_byte_size;
			else return opt<Dwarf_Unsigned>();
		}
		
		iterator_df<type_die> array_type_die::ultimate_element_type(optional_root_arg_decl) const
		{
			iterator_df<type_die> cur = get_concrete_type(opt_r);
			while (cur != iterator_base::END
				 && cur.is_a<array_type_die>())
			{
				cur = cur.as_a<array_type_die>()->get_type(opt_r);
				if (cur != iterator_base::END) cur = cur->get_concrete_type(opt_r);
			}
			return cur;
		}
		
		opt<Dwarf_Unsigned> array_type_die::ultimate_element_count(optional_root_arg_decl) const 
		{
			Dwarf_Unsigned count = 1;
			iterator_df<type_die> cur = get_concrete_type(opt_r);
			while (cur != iterator_base::END
				 && cur.is_a<array_type_die>())
			{
				auto opt_count = cur.as_a<array_type_die>()->element_count(opt_r);
				if (!opt_count) return opt<Dwarf_Unsigned>();
				else 
				{
					count *= *opt_count;
					cur = cur.as_a<type_chain_die>()->get_type(opt_r);
					if (cur != iterator_base::END) cur = cur->get_concrete_type(opt_r);
				}
			}
			return opt<Dwarf_Unsigned>(count);
		}
		
		
/* from spec::structure_type_die */
		opt<Dwarf_Unsigned> structure_type_die::calculate_byte_size(optional_root_arg_decl) const
		{
			// HACK: for now, do nothing
			// We should make this more reliable,
			// but it's difficult because overall size of a struct is
			// language- and implementation-dependent.
			return this->type_die::calculate_byte_size(opt_r);
		}
/* from spec::with_data_members_die */
		bool with_data_members_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, optional_root_arg_decl) const
		{
			if (!t) return false;
			cerr << "Testing with_data_members_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			
			if (get_name() != t.name_here()) return false;
			
			/* We need like-named, like-located members. 
			 * GAH. We really need to canonicalise location lists to do this properly. 
			 * That sounds difficult (impossible in general). Nevertheless for most
			 * structs, it's likely to be that they are identical. */
			
			/* Another GAH: recursive structures. What to do about them? */
			
			auto our_member_children = children().subseq_of<member_die>();
			auto their_member_children = t->children().subseq_of<member_die>();
			auto i_theirs = their_member_children.first;
			for (auto i_memb = our_member_children.first; i_memb != our_member_children.second;
				++i_memb, ++i_theirs)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_member_children.second) return false;
				
				auto this_test_pair = make_pair(
					get_root(opt_r).find(get_offset()).as_a<type_die>(),
					t
				);
				auto recursive_test_set = assuming_equal; recursive_test_set.insert(this_test_pair);
				
				bool types_equal = 
				// presence equal
					(!i_memb->get_type() == !i_theirs->get_type())
				// and if we have one, it's equal to theirs
				&& (!i_memb->get_type() || 
				/* RECURSION: here we may get into an infinite loop 
				 * if equality of get_type() depends on our own equality. 
				 * So we use equal_modulo_assumptions()
				 * which is like operator== but doesn't recurse down 
				 * grey (partially opened)  */
					i_memb->get_type()->equal(i_theirs->get_type(), 
						/* Don't recursively begin the test we're already doing */
						recursive_test_set));
				if (!types_equal) return false;
				
				auto loc1 = i_memb->get_data_member_location();
				auto loc2 = i_theirs->get_data_member_location();
				bool locations_equal = 
					loc1 == loc2;
				if (!locations_equal) return false;
				
				// FIXME: test names too? not for now
			}
			// if they had more, we're unequal
			if (i_theirs != their_member_children.second) return false;
			
			return true;
		}
		iterator_base with_data_members_die::find_definition(optional_root_arg_decl) const
		{
			root_die& r = get_root(opt_r);
			if (!get_declaration(r) || !*get_declaration(r)) 
			{
				/* we are a definition already, but we have to find ourselves :-( */
				return r.find(get_offset());
			}
			cerr << "Looking for definition of declaration " << summary() << endl;
			
			// if we don't have a name, we have no way to proceed
			auto opt_name = get_name(/*r*/);
			if (!opt_name) goto return_no_result;
			else
			{
				string my_name = *opt_name;

				/* Otherwise, we search forwards from our position, for siblings
				 * that have the same name but no "declaration" attribute. */
				auto iter = r.find(get_offset());

				/* Are we a CU-toplevel DIE? We only handle this case at the moment. 
				   PROBLEM:
				   
				   declared C++ classes like like this:
				 <2><1d8d>: Abbrev Number: 56 (DW_TAG_class_type)
				    <1d8e>   DW_AT_name        : (indirect string, offset: 0x17b4): reverse_iterator
				<__gnu_cxx::__normal_iterator<char const*, std::basic_string<char, std::char_traits<
				char>, std::allocator<char> > > >       
				    <1d92>   DW_AT_declaration : 1      

				   The definition of the class has name "reverse_iterator"!
				   The other stuff is encoded in the DW_TAG_template_type_parameter members.
				   These act a lot like typedefs, so we should make them type_chains.
				   
				
				*/
				if (iter.depth() != 2) goto return_no_result;

				iterator_sibs<with_data_members_die> i_sib = iter; ++i_sib;/* i.e. don't check ourselves */; 
				for (; i_sib != iterator_base::END; ++i_sib)
				{
					opt<bool> opt_decl_flag;
					if (i_sib.is_a<with_data_members_die>()
					 && i_sib->get_name() && *i_sib->get_name() == my_name
						&& (opt_decl_flag = i_sib->get_declaration(r), 
							!opt_decl_flag || !*opt_decl_flag))
					{
						cerr << "Found definition " << i_sib->summary() << endl;
						return i_sib;
					}
				}
			}
		return_no_result:
			cerr << "Failed to find definition of declaration " << summary() << endl;
			return iterator_base::END;
		}

		bool variable_die::has_static_storage(optional_root_arg_decl) const
		{
			// don't bother testing whether we have an enclosing subprogram -- too expensive
			//if (nonconst_this->nearest_enclosing(DW_TAG_subprogram))
			//{
				// we're either a local or a static -- skip if local
				root_die& r = get_root(opt_r);
				auto attrs = copy_attrs(r);
				if (attrs.find(DW_AT_location) != attrs.end())
				{
					// HACK: only way to work out whether it's static
					// is to test for frame-relative addressing in the location
					// --- *and* since we can't rely on the compiler to generate
					// DW_OP_fbreg for every frame-relative variable (since it
					// might just use %ebp or %esp directly), rule out any
					// register-relative location whatsoever. FIXME: this might
					// break some code on segmented architectures, where even
					// static storage is recorded in DWARF using 
					// register-relative addressing....
					auto loclist = attrs.find(DW_AT_location)->second.get_loclist();
					
					// if our loclist is empty, we're probably an optimised-out local,
					// so return false
					if (loclist.begin() == loclist.end()) return false;
					
					for (auto i_loc_expr = loclist.begin(); 
						i_loc_expr != loclist.end(); 
						++i_loc_expr)
					{
						for (auto i_instr = i_loc_expr->begin(); 
							i_instr != i_loc_expr->end();
							++i_instr)
						{
							if (i_instr->lr_atom == DW_OP_stack_value)
							{
								/* This means it has *no storage* in the range of the current 
								 * loc_expr, so we guess that it has no storage or stack storage
								 * for its whole lifetime, and return false. */
								return false;
							}

							if (get_spec(r).op_reads_register(i_instr->lr_atom)) return false;
						}
					}
				}
			//}
			return true;
		}
		
/* from spec::with_dynamic_location_die */ 
		opt<Dwarf_Unsigned> 
		with_dynamic_location_die::byte_offset_in_enclosing_type(optional_root_arg_decl,
			bool assume_packed_if_no_location /* = false */) const
		{
			if (get_tag() != DW_TAG_member && get_tag() != DW_TAG_inheritance)
			{
				// we need to be a member or inheritance
				return opt<Dwarf_Unsigned>();
			}
			// if we're a declaration, that's bad
			if (get_declaration() && *get_declaration())
			{
				return opt<Dwarf_Unsigned>();
			}
			
			// we have to find ourselves :-(
			root_die& r = get_root(opt_r);
			auto it = r.find(get_offset());
			auto parent = r.parent(it);
			auto enclosing_type_die = parent.as_a<core::type_die>();
			if (!enclosing_type_die) return opt<Dwarf_Unsigned>();
			
			opt<encap::loclist> data_member_location 
			 = it.is_a<member_die>() ? it.as_a<member_die>()->get_data_member_location(r) 
			 : it.is_a<inheritance_die>() ? it.as_a<inheritance_die>()->get_data_member_location(r)
			 : opt<encap::loclist>();
			if (!data_member_location)
			{
				// if we don't have a location for this field,
				// we tolerate it iff it's the first non-declaration one in a struct/class
				// OR contained in a union
				// OR if the caller passed assume_packed_if_no_location
				// HACK: support class types (and others) here
				auto parent_first_member
				 = enclosing_type_die.children().subseq_of<core::with_dynamic_location_die>().first;
				assert(parent_first_member.base().base() != iterator_base::END);
				while (
					!(parent_first_member.base().base().is_a<member_die>() || parent_first_member.base().base().is_a<inheritance_die>())
					|| (parent_first_member->get_declaration() && *parent_first_member->get_declaration())
				)
				{
					++parent_first_member;
					// at the latest, we should hit ourselves
					assert(parent_first_member.base().base() != iterator_base::END);
				}
				
				// if we are the first member of a struct, or any member of a union, we're okay
				if (it.offset_here() == parent_first_member.base().base().offset_here()
				 || enclosing_type_die.tag_here() == DW_TAG_union_type)
				{
					return opt<Dwarf_Unsigned>(0U);
				}
				
				/* Otherwise we might still be okay. */
				if (assume_packed_if_no_location)
				{
					auto previous_member = parent_first_member;
					// if there is one member or more before us...
					if (previous_member.base().base() != it)
					{
						do
						{
							auto next_member = previous_member;
							// advance to the next non-decl member or inheritance DIE 
							do
							{
								++next_member;
							} while (next_member.base().base() != it
								&& (!(next_member.base().base().is_a<member_die>() || next_member.base().base().is_a<inheritance_die>())
								|| (next_member->get_declaration() && *next_member->get_declaration())));
							// break if we hit ourselves
							if (next_member.base().base() == it) break;
							previous_member = std::move(next_member);
						} while (true); 

						if (previous_member.base().base()) 
						{
							auto prev_memb_t = previous_member->get_type();
							if (prev_memb_t)
							{
								auto opt_prev_byte_size = prev_memb_t->calculate_byte_size();
								if (opt_prev_byte_size)
								{
									/* Do we have an offset for the previous member? */
									auto opt_prev_member_offset = previous_member->byte_offset_in_enclosing_type(
										opt_r, true);

									/* If that succeeded, we can go ahead. */
									if (opt_prev_member_offset)
									{
										return opt<Dwarf_Unsigned>(*opt_prev_member_offset + *opt_prev_byte_size);
									}
								}
							}
						}
					}
				}
				
				// if we got here, we really can't figure it out
				std::cerr << "Warning: encountered DWARF member lacking a location: "
					<< it << std::endl;
				return opt<Dwarf_Unsigned>();
			}
			else if (data_member_location->size() != 1)
			{
				std::cerr << "Bad location: " << *data_member_location << std::endl;
				goto location_not_understood;
			}
			else
			{
				/* If we have an indirection here, we will get some memory access 
				 * happening, and our evaluator should bail out. Q: how? A. DW_OP_deref
				 * has no implementation, because we don't pass a memory. 
				 *
				 * FIXME: when we add support for memory operations, the error we
				 * get will be different, and we need to update the catch case. */
				try {
					return dwarf::lib::evaluator(
						data_member_location->at(0), 
						it.enclosing_cu().spec_here(), 
						std::stack<Dwarf_Unsigned>(std::deque<Dwarf_Unsigned>(1, 0UL))).tos();
				} 
				catch (dwarf::lib::Not_supported)
				{
					goto location_not_understood;
				}
			}
		location_not_understood:
				// error
				std::cerr << "Warning: encountered DWARF member with location I didn't understand: "
					<< it << std::endl;
				return opt<Dwarf_Unsigned>();
		}
		
		boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned> 
		with_static_location_die::file_relative_intervals(
		
			root_die& r, 
		
			sym_binding_t (*sym_resolve)(const std::string& sym, void *arg), 
			void *arg /* = 0 */) const
		{
			encap::attribute_map attrs = d.copy_attrs(r);
			
			using namespace boost::icl;
			auto& right_open = interval<Dwarf_Addr>::right_open;
			interval_map<Dwarf_Addr, Dwarf_Unsigned> retval;

			// HACK: if we're a local variable, return false. This function
			// only deals with static storage. Mostly the restriction is covered
			// by the fact that only certain tags are with_static_location_dies,
			// but both locals and globals show up with DW_TAG_variable.
			if (this->get_tag() == DW_TAG_variable &&
				!dynamic_cast<const variable_die *>(this)->has_static_storage())
				goto out;
			else
			{
				auto found_low_pc = attrs.find(DW_AT_low_pc);
				auto found_high_pc = attrs.find(DW_AT_high_pc);
				auto found_ranges = attrs.find(DW_AT_ranges);
				auto found_location = attrs.find(DW_AT_location);
				auto found_mips_linkage_name = attrs.find(DW_AT_MIPS_linkage_name); // HACK: MIPS should...
				auto found_linkage_name = attrs.find(DW_AT_linkage_name); // ... be in a non-default spec

				if (found_ranges != attrs.end())
				{
					iterator_df<compile_unit_die> i_cu = r.cu_pos(d.enclosing_cu_offset_here());
					auto rangelist = i_cu->normalize_rangelist(found_ranges->second.get_rangelist());
					Dwarf_Unsigned cumulative_bytes_seen = 0;
					for (auto i_r = rangelist.begin(); i_r != rangelist.end(); ++i_r)
					{
						auto& r = *i_r;
						if (r.dwr_addr1 == r.dwr_addr2) 
						{
							// I have seen this happen...
							//assert(r.dwr_addr1 == 0);
							continue;
						}
						auto ival = interval<Dwarf_Addr>::right_open(r.dwr_addr1, r.dwr_addr2);
						//clog << "Inserting interval " << ival << endl;
						// HACK: icl doesn't like zero codomain values??
						cumulative_bytes_seen += r.dwr_addr2 - r.dwr_addr1;
						retval.insert(
							make_pair(
								ival, 
								cumulative_bytes_seen 
							)
						);
						assert(retval.find(ival) != retval.end());
						assert(r.dwr_addr2 > r.dwr_addr1);
					}
					// sanity check: assert that the first interval is included
					assert(rangelist.size() == 0 
						|| (rangelist.begin())->dwr_addr1 == (rangelist.begin())->dwr_addr2
						|| retval.find(right_open(
							(rangelist.begin())->dwr_addr1, 
							(rangelist.begin())->dwr_addr2
							)) != retval.end());
				}
				else if (found_low_pc != attrs.end() && found_high_pc != attrs.end() && found_high_pc->second.get_form() == encap::attribute_value::ADDR)
				{
					auto hipc = found_high_pc->second.get_address().addr;
					auto lopc = found_low_pc->second.get_address().addr;
					if (hipc > lopc)
					{
						retval.insert(make_pair(right_open(
							lopc, 
							hipc
						), hipc - lopc));
					} else assert(hipc == lopc);
				}
				else if (found_low_pc != attrs.end() && found_high_pc != attrs.end() && found_high_pc->second.get_form() == encap::attribute_value::UNSIGNED)
				{
					auto lopc = found_low_pc->second.get_address().addr;
					auto hipc = lopc + found_high_pc->second.get_unsigned();
					if (hipc > 0) {
						retval.insert(make_pair(right_open(
								lopc, 
								hipc
							), hipc - lopc));
					}
				}
				else if (found_location != attrs.end())
				{
					/* Location lists can be vaddr-dependent, where vaddr is the 
					 * offset of the current PC within the containing subprogram.
					 * Since we're a with_static_location_die, we *probably* don't
					 * have vaddr-dependent location. FIXME: check this is okay. */

					optional<Dwarf_Unsigned> opt_byte_size;
					auto found_byte_size = attrs.find(DW_AT_byte_size);
					if (found_byte_size != attrs.end())
					{
						opt_byte_size = found_byte_size->second.get_unsigned();
					}
					else
					{	
						/* Look for the type. "Type" means something different
						 * for a subprogram, which should be covered by the
						 * high_pc/low_pc and ranges cases, so assert that
						 * we don't have one of those. */
						assert(this->get_tag() != DW_TAG_subprogram);
						auto found_type = attrs.find(DW_AT_type);
						if (found_type == attrs.end()) goto out;
						else
						{
							iterator_df<type_die> t = r.find(found_type->second.get_ref().off);
							auto calculated_byte_size = t->calculate_byte_size(r);
							assert(calculated_byte_size);
							opt_byte_size = *calculated_byte_size; // assign to *another* opt
						}
					}
					assert(opt_byte_size);
					Dwarf_Unsigned byte_size = *opt_byte_size;
					if (byte_size == 0)
					{
						cerr << "Zero-length object: " << summary() << endl;
						goto out;
					}
					
					auto loclist = found_location->second.get_loclist();
					std::vector<std::pair<dwarf::encap::loc_expr, Dwarf_Unsigned> > expr_pieces;
					try
					{
						expr_pieces = loclist.loc_for_vaddr(0).pieces();
					}
					catch (No_entry)
					{
						if (loclist.size() > 0)
						{
							cerr << "Vaddr-dependent static location " << *this << endl;
						}
						else cerr << "Static var with no location: " << *this << endl;
						//if (loclist.size() > 0)
						//{
						//	expr_pieces = loclist.begin()->pieces();
						//}
						/*else*/ goto out;
					}
					
					try
					{
						Dwarf_Off current_offset_within_object = 0UL;
						for (auto i = expr_pieces.begin(); i != expr_pieces.end(); ++i)
						{
							/* Evaluate this piece. */
							Dwarf_Unsigned piece_size = i->second;
							Dwarf_Unsigned piece_start = dwarf::lib::evaluator(i->first,
								this->get_spec(r)).tos();

							/* If we have only one piece, it means there might be no DW_OP_piece,
							 * so the size of the piece will be unreliable (possibly zero). */
							if (expr_pieces.size() == 1 && expr_pieces.begin()->second == 0)
							{
								piece_size = byte_size;
							}
							// HACK: increment early to avoid icl zero bug
							current_offset_within_object += piece_size;

							retval.insert(make_pair(
								right_open(piece_start, piece_start + piece_size),
								current_offset_within_object
							));
						}
						assert(current_offset_within_object == byte_size);
					}
					catch (Not_supported)
					{
						// some opcode we don't recognise
						cerr << "Unrecognised opcode in " << *this << endl;
						goto out;
					}

				}
				else if (sym_resolve &&
					(found_mips_linkage_name != attrs.end()
					|| found_linkage_name != attrs.end()))
				{
					std::string linkage_name;

					// prefer the DWARF 4 attribute to the MIPS/GNU/... extension
					if (found_linkage_name != attrs.end()) linkage_name 
					 = found_linkage_name->second.get_string();
					else 
					{
						assert(found_mips_linkage_name != attrs.end());
						linkage_name = found_mips_linkage_name->second.get_string();
					}

					sym_binding_t binding;
					try
					{
						binding = sym_resolve(linkage_name, arg);
						retval.insert(make_pair(right_open(
								binding.file_relative_start_addr,
								binding.file_relative_start_addr + binding.size
							), binding.size)
						);
					}
					catch (lib::No_entry)
					{
						std::cerr << "Warning: couldn't resolve linkage name " << linkage_name
							<< " for DIE " << *this << std::endl;
					}
				}

			}
		out:
			//cerr << "Intervals of " << this->summary() << ": " << retval << endl;
			return retval;
		}

		opt<Dwarf_Off> // returns *offset within the element*
		with_static_location_die::spans_addr(Dwarf_Addr file_relative_address,
			root_die& r, 
			sym_binding_t (*sym_resolve)(const std::string& sym, void *arg), 
			void *arg /* = 0 */) const
		{
			// FIXME: cache intervals
			auto intervals = file_relative_intervals(r, sym_resolve, arg);
			auto found = intervals.find(file_relative_address);
			if (found != intervals.end())
			{
				Dwarf_Off interval_end_offset_from_object_start = found->second;
				assert(file_relative_address >= found->first.lower());
				Dwarf_Off addr_offset_from_interval_start
				 = file_relative_address - found->first.lower();
				assert(file_relative_address < found->first.upper());
				auto interval_size = found->first.upper() - found->first.lower();
				return interval_end_offset_from_object_start
				 - interval_size
				 + addr_offset_from_interval_start;
			}
			else return opt<Dwarf_Off>();
		}
/* helpers */		
// 		static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc, Dwarf_Addr high_pc);
// 		static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc, Dwarf_Addr high_pc)
// 		{
// 			Dwarf_Unsigned opcodes[] 
// 			= { DW_OP_constu, low_pc, 
// 				DW_OP_piece, high_pc - low_pc };
// 
// 			/* */
// 			encap::loclist list(
// 				encap::loc_expr(
// 					opcodes, 
// 					low_pc, std::numeric_limits<Dwarf_Addr>::max()
// 				)
// 			); 
//             return list;
//         }
//         static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc);
//         static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc)
//         {
//             Dwarf_Unsigned opcodes[] 
//             = { DW_OP_constu, low_pc };
// 			/* FIXME: I don't think we should be using the max Dwarf_Addr here -- 
// 			 * the libdwarf manual claims we should set them both to zero. */
//             encap::loclist list(encap::loc_expr(opcodes, 0, std::numeric_limits<Dwarf_Addr>::max())); 
//             return list;
// 			
// 			/* NOTE: libdwarf seems to give us ADDR_MAX as the high_pc 
// 			 * in the case of single-shot location expressions (i.e. not lists)
// 			 * encoded as attributes. We don't re-encode them 
// 			 * when they pass through: see lib::loclist::loclist in lib.hpp 
// 			 * and the block_as_dwarf_expr case in attr.cpp. */
//         }
		encap::loclist with_static_location_die::get_static_location(optional_root_arg_decl) const
        {
        	auto attrs = copy_attrs(get_root(opt_r));
            if (attrs.find(DW_AT_location) != attrs.end())
            {
            	return attrs.find(DW_AT_location)->second.get_loclist();
            }
            else
        	/* This is a dieset-relative address. */
            if (attrs.find(DW_AT_low_pc) != attrs.end() 
            	&& attrs.find(DW_AT_high_pc) != attrs.end())
            {
				auto low_pc = attrs.find(DW_AT_low_pc)->second.get_address().addr;
				auto high_pc = attrs.find(DW_AT_high_pc)->second.get_address().addr;
				Dwarf_Unsigned opcodes[] 
				= { DW_OP_constu, low_pc, 
					DW_OP_piece, high_pc - low_pc };

				/* If we're a "static" object, what are the validity vaddrs of our 
				 * loclist entry? It's more than just our own vaddrs. Using the whole
				 * vaddr range seems sensible. But (FIXME) it's not very DWARF-compliant. */
				encap::loclist list(
					encap::loc_expr(
						opcodes, 
						0, std::numeric_limits<Dwarf_Addr>::max()
					)
				); 
				return list;
			}
			else
			{
				assert(attrs.find(DW_AT_low_pc) != attrs.end());
				auto low_pc = attrs.find(DW_AT_low_pc)->second.get_address().addr;
				Dwarf_Unsigned opcodes[] 
				 = { DW_OP_constu, low_pc };
				/* FIXME: I don't think we should be using the max Dwarf_Addr here -- 
				 * the libdwarf manual claims we should set them both to zero. */
				encap::loclist list(
					encap::loc_expr(
						opcodes, 
						0, std::numeric_limits<Dwarf_Addr>::max()
					)
				); 
				return list;
			}
		}
		
		/* We would define this inside spans_addr_in_frame_locals_or_args, 
		 * but we can't use forward_constructors (a "member template")
		 * inside a local class. */
		struct frame_subobject_iterator :  public iterator_bf<with_dynamic_location_die>
		{
			typedef iterator_bf<with_dynamic_location_die> super;
			void increment()
			{
				/* If our current DIE is 
				 * a with_dynamic_location_die
				 * OR
				 * is in the "interesting set"
				 * of DIEs that have no location but might contain such DIEs,
				 * we increment *with* enqueueing children.
				 * Otherwise we increment without enqueueing children.
				 */
				if (dynamic_cast<with_dynamic_location_die*>(&dereference())) { 
					super::increment();
				} else {
					switch (tag_here())
					{
						case DW_TAG_lexical_block:
							super::increment();
							break;
						default:
							super::increment_skipping_subtree();
							break;
					}					
				}
			}

			forward_constructors(super, frame_subobject_iterator)
		};
/* from spec::subprogram_die */
		opt< pair<Dwarf_Off, iterator_df<with_dynamic_location_die> > >
        subprogram_die::spans_addr_in_frame_locals_or_args( 
            	    Dwarf_Addr absolute_addr, 
					root_die& r, 
                    Dwarf_Off dieset_relative_ip, 
                    Dwarf_Signed *out_frame_base,
                    dwarf::lib::regs *p_regs/* = 0*/) const
        {
			typedef opt< pair<Dwarf_Off, iterator_df<with_dynamic_location_die> > >
				return_type;
			
        	/* auto nonconst_this = const_cast<subprogram_die *>(this); */
        	assert(this->get_frame_base());

			/* We have to find ourselves. :-( */
			auto i = r.find(get_offset()); 
			assert(i != iterator_base::END);
			
			// Calculate the vaddr which selects a loclist element
			auto frame_base_loclist = *get_frame_base();
			iterator_df<compile_unit_die> enclosing_cu
			 = r.cu_pos(i.enclosing_cu_offset_here());
			cerr << "Enclosing CU is " << enclosing_cu->summary() << endl;
			Dwarf_Addr low_pc = enclosing_cu->get_low_pc()->addr;
			assert(low_pc <= dieset_relative_ip);
			Dwarf_Addr vaddr = dieset_relative_ip - low_pc;
			/* Now calculate our frame base address. */
            auto frame_base_addr = dwarf::lib::evaluator(
                frame_base_loclist,
                vaddr,
                get_spec(r),
                p_regs).tos();
            if (out_frame_base) *out_frame_base = frame_base_addr;
            
			auto child = r.first_child(i);
            if (child == iterator_base::END) return return_type();
			/* Now we walk children
			 * (not just immediate children, because more might hide under lexical_blocks), 
			 * looking for with_dynamic_location_dies, and 
			 * call spans_addr on what we find.
			 * We skip contained DIEs that do not contain objects located in this frame. 
			 */
			//abstract_dieset::bfs_policy bfs_state;
            frame_subobject_iterator start_iter(child);
            std::cerr << "Exploring stack-located children of " << summary() << std::endl;
            unsigned initial_depth = start_iter.depth();
            for (auto i_bfs = start_iter;
            		i_bfs.depth() >= initial_depth;
                    ++i_bfs)
            {
            	std::cerr << "Considering whether DIE has stack location: " 
					<< i_bfs->summary() << std::endl;
            	auto with_stack_loc = dynamic_cast<with_dynamic_location_die*>(&i_bfs.dereference());
                if (!with_stack_loc) continue;
                
                opt<Dwarf_Off> result = with_stack_loc->spans_addr(absolute_addr,
                	frame_base_addr,
					r, 
                    dieset_relative_ip,
                    p_regs);
                if (result) return make_pair(
					*result, 
					iterator_df<with_dynamic_location_die>(i_bfs)
				);
            }
            return return_type();
        }
		iterator_df<type_die> subprogram_die::get_return_type(optional_root_arg_decl) const
		{
			return get_type(); 
		}
/* from type_describing_subprogram_die */
		bool type_describing_subprogram_die::is_variadic(optional_root_arg_decl) const
		{
			/* We have to find ourselves. :-( */
			root_die& r = get_root(opt_r);
			auto i = r.find(get_offset());
			assert(i != iterator_base::END);
			auto children = i.children_here();
			auto unspec = children.subseq_of<unspecified_parameters_die>();
			return unspec.first != unspec.second;
		}
		bool type_describing_subprogram_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, optional_root_arg_decl) const
		{
			if (!t) return false;
			cerr << "Testing type_describing_subprogram_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			
			if (get_name() != t.name_here()) return false;
			
			auto other_sub_t = t.as_a<type_describing_subprogram_die>();
			
			bool return_types_equal = 
				// presence equal
					(!get_return_type() == !other_sub_t->get_return_type())
				// and if we have one, it's equal to theirs
				&& (!get_return_type() || get_return_type()->equal(other_sub_t->get_return_type(), assuming_equal));
			if (!return_types_equal) return false;
			
			bool variadicness_equal
			 = is_variadic() == other_sub_t->is_variadic();
			if (!variadicness_equal) return false;
			
			/* We need like-named, like-located fps. 
			 * GAH. We really need to canonicalise location lists to do this properly. 
			 * That sounds difficult (impossible in general). */
			auto our_fps = children().subseq_of<formal_parameter_die>();
			auto their_fps = t->children().subseq_of<formal_parameter_die>();
			auto i_theirs = their_fps.first;
			for (auto i_fp = our_fps.first; i_fp != our_fps.second;
				++i_fp, ++i_theirs)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_fps.second) return false;
				
				bool types_equal = 
				// presence equal
					(!i_fp->get_type() == !i_theirs->get_type())
				// and if we have one, it's equal to theirs
				&& (!i_fp->get_type() || i_fp->get_type()->equal(i_theirs->get_type(), assuming_equal));
				
				if (!types_equal) return false;
				
				bool locations_equal = 
					(i_fp->get_location() == i_theirs->get_location());
				if (!locations_equal) return false;
				
				// FIXME: test names too? not for now
			}
			// if they had more, we're unequal
			if (i_theirs != their_fps.second) return false;
			
			return true;
		}
/* from subroutine_type_die */
		iterator_df<type_die> subroutine_type_die::get_return_type(optional_root_arg_decl) const
		{
			return get_type();
		}
/* from spec::with_dynamic_location_die */
		iterator_df<program_element_die> 
		with_dynamic_location_die::get_instantiating_definition(optional_root_arg_decl) const
		{
			/* We want to return a parent DIE describing the thing whose instances
			 * contain instances of us. 
			 *
			 * If we're a member or inheritance, it's our nearest enclosing type.
			 * If we're a variable or fp, it's our enclosing subprogram.
			 * This might be null if we're actually a static variable. */

			/* We have to find ourselves. :-( */ 
			root_die& r = get_root(opt_r);
			auto i = r.find(get_offset());
			assert(i != iterator_base::END);

			// HACK: this should arguably be in overrides for formal_parameter and variable
			if (get_tag() == DW_TAG_formal_parameter
			||  get_tag() == DW_TAG_variable) 
			{
				return i.nearest_enclosing(DW_TAG_subprogram);
			}
			else // return the nearest enclosing data type
			{
				auto candidate = i.parent();
				while (candidate != iterator_base::END
					&& !dynamic_cast<type_die *>(&candidate.dereference()))
				{
					candidate = candidate.parent();
				}
				return candidate; // might be END
			}
		}

/* from spec::with_dynamic_location_die */
		opt<Dwarf_Off> 
		with_dynamic_location_die::spans_stack_addr(
                    Dwarf_Addr absolute_addr,
                    Dwarf_Signed frame_base_addr,
					root_die& r, 
                    Dwarf_Off dieset_relative_ip,
                    dwarf::lib::regs *p_regs) const
        {
        	auto attrs = copy_attrs(r);
			if (attrs.find(DW_AT_location) == attrs.end())
			{
				cerr << "Warning: " << this->summary() << " has no DW_AT_location; "
					<< "assuming it does not cover any stack locations." << endl;
				return opt<Dwarf_Off>();
			}
            auto base_addr = calculate_addr_on_stack(
				frame_base_addr,
				r, 
				dieset_relative_ip,
				p_regs);
			std::cerr << "Calculated that an instance of DIE" << summary()
				<< " has base addr 0x" << std::hex << base_addr << std::dec;
            assert(attrs.find(DW_AT_type) != attrs.end());
            auto size = *(attrs.find(DW_AT_type)->second.get_refiter_is_type()->calculate_byte_size(r));
			std::cerr << " and size " << size
				<< ", to be tested against absolute addr 0x"
				<< std::hex << absolute_addr << std::dec << std::endl;
            if (absolute_addr >= base_addr
            &&  absolute_addr < base_addr + size)
            {
 				return absolute_addr - base_addr;
            }
            return opt<Dwarf_Off>();
        }
/* from with_dynamic_location_die, stack-based cases */
		encap::loclist formal_parameter_die::get_dynamic_location(optional_root_arg_decl) const
		{
			/* These guys are probably relative to a frame base. 
			   If they're not, it's an error. So we rewrite the loclist
			   so that it's relative to a frame base. */
			
			// see note in expr.hpp
			if (!this->get_location()) return encap::loclist::NO_LOCATION; //(/*encap::loc_expr::NO_LOCATION*/);
			return absolute_loclist_to_additive_loclist(
				*this->get_location());
		
		}
		encap::loclist variable_die::get_dynamic_location(optional_root_arg_decl) const
		{
			// see note in expr.hpp
			if (!this->get_location()) return encap::loclist::NO_LOCATION; //(/*encap::loc_expr::NO_LOCATION*/);
			
			// We have to find ourselves. :-( 
			root_die& r = get_root(opt_r);
			auto i = r.find(get_offset());
			assert(i != iterator_base::END);
			
			// we need an enclosing subprogram or lexical_block
			auto i_lexical = i.nearest_enclosing(DW_TAG_lexical_block);
			auto i_subprogram = i.nearest_enclosing(DW_TAG_subprogram);
			if (i_lexical == iterator_base::END && i_subprogram == iterator_base::END)
			{ throw No_entry(); }
			
			return 	absolute_loclist_to_additive_loclist(
				*this->get_location());
		}
/* from spec::with_dynamic_location_die */
		opt<Dwarf_Off> with_dynamic_location_die::spans_addr_in_object(
                    Dwarf_Addr absolute_addr,
                    Dwarf_Signed object_base_addr,
					root_die& r, 
                    Dwarf_Off dieset_relative_ip,
                    dwarf::lib::regs *p_regs) const
        {
        	auto attrs = copy_attrs(r);
            auto base_addr = calculate_addr_in_object(
				object_base_addr, r, dieset_relative_ip, p_regs);
            assert(attrs.find(DW_AT_type) != attrs.end());
            auto size = *(attrs.find(DW_AT_type)->second.get_refiter_is_type()->calculate_byte_size(r));
            if (absolute_addr >= base_addr
            &&  absolute_addr < base_addr + size)
            {
 				return absolute_addr - base_addr;
            }
            return opt<Dwarf_Off>();
        }
/* from with_dynamic_location_die, object-based cases */
		encap::loclist member_die::get_dynamic_location(optional_root_arg_decl) const
		{
			/* These guys have loclists that add to what's on the
			   top-of-stack, which is what we want. */
			opt<encap::loclist> opt_location = get_data_member_location(opt_r);
			return opt_location ? *opt_location : encap::loclist();
		}
		encap::loclist inheritance_die::get_dynamic_location(optional_root_arg_decl) const
		{
			opt<encap::loclist> opt_location = get_data_member_location(opt_r);
			return opt_location ? *opt_location : encap::loclist();
		}
/* from spec::with_dynamic_location_die */
		Dwarf_Addr 
		with_dynamic_location_die::calculate_addr_on_stack(
				Dwarf_Addr frame_base_addr,
				root_die& r, 
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs/* = 0*/) const
		{
        	auto attrs = copy_attrs(r);
            assert(attrs.find(DW_AT_location) != attrs.end());
			
			/* We have to find ourselves. :-( Well, almost -- enclosing CU. */
			auto found = r.cu_pos(get_enclosing_cu_offset());
			iterator_df<compile_unit_die> i_cu = found;
			assert(i_cu != iterator_base::END);
			Dwarf_Addr dieset_relative_cu_base_ip
			 = i_cu->get_low_pc() ? i_cu->get_low_pc()->addr : 0;
			
			if (dieset_relative_ip < dieset_relative_cu_base_ip)
			{
				cerr << "Warning: bad relative IP (0x" << std::hex << dieset_relative_ip << std::dec
					<< ") for stack location of DIE in compile unit "
					<< i_cu 
					<< ": " << *this << endl;
				throw No_entry();
			}
			
			auto& loclist = attrs.find(DW_AT_location)->second.get_loclist();
			auto intervals = loclist.intervals();
			assert(intervals.begin() != intervals.end());
			auto first_interval = intervals.begin();
			auto last_interval = intervals.end(); --last_interval;
			encap::loc_expr fb_loc_expr((Dwarf_Unsigned[]) { DW_OP_plus_uconst, frame_base_addr }, 
					first_interval->lower(), last_interval->upper());
			encap::loclist fb_loclist(fb_loc_expr);
			
			auto rewritten_loclist = encap::rewrite_loclist_in_terms_of_cfa(
				loclist, 
				r.get_frame_section(),
				fb_loclist
			);
			cerr << "After rewriting, loclist is " << rewritten_loclist << endl;
			
			return (Dwarf_Addr) dwarf::lib::evaluator(
				rewritten_loclist,
				dieset_relative_ip // needs to be CU-relative
				 - dieset_relative_cu_base_ip,
				found.spec_here(),
				p_regs,
				frame_base_addr).tos();
		}
		Dwarf_Addr
		with_dynamic_location_die::calculate_addr_in_object(
				Dwarf_Addr object_base_addr,
				root_die& r, 
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs /*= 0*/) const
		{
        	auto attrs = copy_attrs(r);
			iterator_df<compile_unit_die> i_cu = r.cu_pos(get_enclosing_cu_offset());
            assert(attrs.find(DW_AT_data_member_location) != attrs.end());
			return (Dwarf_Addr) dwarf::lib::evaluator(
				attrs.find(DW_AT_data_member_location)->second.get_loclist(),
				dieset_relative_ip == 0 ? 0 : // if we specify it, needs to be CU-relative
				 - (i_cu->get_low_pc() ? 
				 	i_cu->get_low_pc()->addr : (Dwarf_Addr)0),
				i_cu.spec_here(), 
				p_regs,
				object_base_addr, // ignored
				std::stack<Dwarf_Unsigned>(std::deque<Dwarf_Unsigned>(1, object_base_addr))).tos();
		}
/* from spec::with_named_children_die */
//         std::shared_ptr<spec::basic_die>
//         with_named_children_die::named_child(const std::string& name) 
//         { 
// 			try
//             {
//             	for (auto current = this->get_first_child();
//                 		; // terminates by exception
//                         current = current->get_next_sibling())
//                 {
//                 	if (current->get_name() 
//                     	&& *current->get_name() == name) return current;
//                 }
//             }
//             catch (No_entry) { return shared_ptr<spec::basic_die>(); }
//         }
// 
//         std::shared_ptr<spec::basic_die> 
//         with_named_children_die::resolve(const std::string& name) 
//         {
//             std::vector<std::string> multipart_name;
//             multipart_name.push_back(name);
//             return resolve(multipart_name.begin(), multipart_name.end());
//         }
// 
//         std::shared_ptr<spec::basic_die> 
//         with_named_children_die::scoped_resolve(const std::string& name) 
//         {
//             std::vector<std::string> multipart_name;
//             multipart_name.push_back(name);
//             return scoped_resolve(multipart_name.begin(), multipart_name.end());
//         }
/* from spec::compile_unit_die */
		encap::rangelist
		compile_unit_die::normalize_rangelist(const encap::rangelist& rangelist/*, optional_root_arg_decl*/) const
		{
			encap::rangelist retval;
			/* We create a rangelist that has no address selection entries. */
			for (auto i = rangelist.begin(); i != rangelist.end(); ++i)
			{
				switch(i->dwr_type)
				{
					case DW_RANGES_ENTRY:
						retval.push_back(*i);
					break;
					case DW_RANGES_ADDRESS_SELECTION: {
						assert(i->dwr_addr1 == 0xffffffff || i->dwr_addr1 == 0xffffffffffffffffULL);
						assert(false);
					} break;
					case DW_RANGES_END: 
						assert(i->dwr_addr1 == 0);
						assert(i+1 == rangelist.end()); 
						retval.push_back(*i);
						break;
					default: assert(false); break;
				}
			}
			return retval;
		}

		opt<Dwarf_Unsigned> compile_unit_die::implicit_array_base(optional_root_arg_decl) const
		{
			switch(get_language(opt_r))
			{
				/* See DWARF 3 sec. 5.12! */
				case DW_LANG_C:
				case DW_LANG_C89:
				case DW_LANG_C_plus_plus:
				case DW_LANG_C99:
					return opt<Dwarf_Unsigned>(0UL);
				case DW_LANG_Fortran77:
				case DW_LANG_Fortran90:
				case DW_LANG_Fortran95:
					return opt<Dwarf_Unsigned>(1UL);
				default:
					return opt<Dwarf_Unsigned>();
			}
		}
		iterator_df<type_die> compile_unit_die::implicit_enum_base_type(optional_root_arg_decl) const
		{
			if (cached_implicit_enum_base_type) return cached_implicit_enum_base_type; // FIXME: cache "not found" result too
		
			root_die& r = get_root(opt_r);
			switch(get_language(r))
			{
				case DW_LANG_C:
				case DW_LANG_C89:
				case DW_LANG_C_plus_plus:
				case DW_LANG_C99: {
					const char *attempts[] = { "signed int", "int" };
					size_t total_attempts = sizeof attempts / sizeof attempts[0];
					for (unsigned i_attempt = 0; i_attempt < total_attempts; ++i_attempt)
					{
						auto found = named_child(attempts[i_attempt], opt_r);
						if (found != iterator_base::END && found.is_a<type_die>())
						{
							cached_implicit_enum_base_type = found.as_a<type_die>();
							return found;
						}
					}
					assert(false && "enum but no int or signed int");
				}
				default:
					return iterator_base::END;
			}
		}
	}
} // end namespace dwarf
