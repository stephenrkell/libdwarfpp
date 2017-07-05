/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * lib.cpp: basic C++ wrapping of libdwarf C API.
 *
 * Copyright (c) 2008--17, Stephen Kell.
 */

#include "dwarfpp/lib.hpp"
#include "dwarfpp/expr.hpp" /* for absolute_loclist_to_additive_loclist */
#include "dwarfpp/frame.hpp"

#include <srk31/indenting_ostream.hpp>
#include <srk31/algorithm.hpp>
#include <sstream>
#include <libelf.h>

namespace dwarf
{
	namespace core
	{
		using std::pair;
		using std::make_pair;
		using std::string;
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
				encap::attribute_value v(attrs[i], attrs.d, attrs.d.get_constructing_root());
				s << ": " << v << endl;
			}
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
			/* For most DIEs, we just return ourselves if we don't have DW_AT_specification
			 * and nothing if we do. */
			if (has_attr(DW_AT_specification) && 
				attr(DW_AT_specification).get_flag())
			{
				return iterator_base::END;
			} else return find_self();
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
		root_die::find_visible_named_grandchild(const string& name)
		{
			vector<string> name_vec(1, name);
			std::vector<iterator_base > found;
			resolve_all_visible_from_root(name_vec.begin(), name_vec.end(), 
				found, 1);
			return found.size() > 0 ? *found.begin() : iterator_base::END;
		}
		
		std::vector<iterator_base>
		root_die::find_all_visible_named_grandchildren(const string& name)
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
				
		dwarf_current_factory_t dwarf_current_factory;
		basic_die *dwarf_current_factory_t::make_non_cu_payload(abstract_die&& h, root_die& r)
		{
			basic_die *p;
			Die d(std::move(dynamic_cast<Die&&>(h)));
			assert(d.tag_here() != DW_TAG_compile_unit);
			switch (d.tag_here())
			{
#define factory_case(name, ...) \
case DW_TAG_ ## name: p = new name ## _die(d.spec_here(), std::move(d.handle)); break; // FIXME: not "basic_die"...
#include "dwarf-current-factory.h"
#undef factory_case
				default: p = new basic_die(d.spec_here(), std::move(d.handle)); break;
			}
			return p;
		}
		
		compile_unit_die *factory::make_cu_payload(abstract_die&& h, root_die& r)
		{
			// Key point: we're not allowed to call Die::spec_here() for this handle. 
			// We could write libdwarf-level code to grab the version stamp and 
			// so on... for now, just construct the thing.
			Die d(std::move(dynamic_cast<Die&&>(h)));
			Dwarf_Off off = d.offset_here();
			auto p = new compile_unit_die(dwarf::spec::dwarf_current, std::move(d.handle));
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
			//new compile_unit_die(dwarf::spec::dwarf_current, Die::handle_type(nullptr, nullptr));
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
					basic_die(parent.depth() >= 1 ? parent.spec_here() : DEFAULT_DWARF_SPEC, \
					    parent.root()), \
					in_memory_abstract_die(parent.root(), \
							  parent.is_root_position() ? \
							  parent.root().fresh_cu_offset() \
							: parent.root().fresh_offset_under(/*parent.root().enclosing_cu(parent)*/parent), \
						parent.is_root_position() ? 0 : parent.enclosing_cu_offset_here(), \
						DW_TAG_ ## name), \
					name ## _die(parent.depth() >= 1 ? parent.spec_here() : DEFAULT_DWARF_SPEC) \
				{ \
					if (parent.is_root_position()) m_cu_offset = m_offset; \
					parent.root().live_dies.insert( \
					    make_pair(this->in_memory_abstract_die::get_offset(), this) \
					); \
				 } \
				root_die& get_root() const \
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
				encap::attribute_map copy_attrs() const \
				{ return this->in_memory_abstract_die::copy_attrs(); } \
				inline spec& get_spec(root_die& r) const \
				{ return this->in_memory_abstract_die::get_spec(r); } \
				/* also override the wider attribute API from basic_die */ \
				/* get all attrs in one go */ \
				virtual encap::attribute_map all_attrs() const \
				{ return copy_attrs(); } \
				/* get a single attr */ \
				virtual encap::attribute_value attr(Dwarf_Half a) const \
				{ if (has_attr(a)) return m_attrs.find(a)->second; else assert(false); } \
				/* get all attrs in one go, seeing through abstract_origin / specification links */ \
				/* -- this one should work already: virtual encap::attribute_map find_all_attrs() const; */ \
				/* get a single attr, seeing through abstract_origin / specification links */ \
				/* -- this one should work already: virtual encap::attribute_value find_attr(Dwarf_Half a) const; */ \
			};
#include "dwarf-current-factory.h"
#undef factory_case

			if (tag == DW_TAG_compile_unit)
			{
				return make_new_cu(r, [parent](){ return new in_memory_compile_unit_die(parent); });
			}
			
			//Dwarf_Off parent_off = parent.offset_here();
			//Dwarf_Off new_off = /*parent.is_root_position() ? r.fresh_cu_offset() : */ r.fresh_offset_under(r.enclosing_cu(parent));
			//Dwarf_Off cu_off = /*(tag == DW_TAG_compile_unit) ? new_off : */ parent.enclosing_cu_offset_here();
			
			basic_die *ret = nullptr;
			switch (tag)
			{
#define factory_case(name, ...) \
case DW_TAG_ ## name: \
			ret = new in_memory_ ## name ## _die(parent); break;
#include "dwarf-current-factory.h"
				default: return nullptr;
			}
#undef factory_case
			return ret;
			
			if (parent.depth() == 1)
			{
				auto it = r.pos(ret->get_offset(), parent.depth() + 1);
				if (it.global_name_here())
				{
					r.visible_named_grandchildren_cache.insert(make_pair(*it.global_name_here(), ret->get_offset()));
				}
			}
		}
		
		basic_die *dwarf_current_factory_t::dummy_for_tag(Dwarf_Half tag)
		{
			static basic_die dummy_basic(dwarf::spec::dwarf_current);
#define factory_case(name, ...) \
static name ## _die dummy_ ## name(dwarf::spec::dwarf_current); 
#include "dwarf-current-factory.h"
#undef factory_case
			switch (tag)
			{
#define factory_case(name, ...) \
case DW_TAG_ ## name: return &dummy_ ## name;
#include "dwarf-current-factory.h"
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
			core::debug() << "libdwarf error!" << std::endl;
			throw Error(e, errarg); /* Whoever catches this should also dwarf_dealloc the Dwarf_Error_s. */
		}
	} // end namespace lib
	
	namespace core
	{
		void type_iterator_df::increment(bool skip_dependencies /* = false */)
		{
			assert(base() == m_stack.back().first);
			/* We are doing the usual incremental depth-first traversal, which means
			 * 1. try to move deeper
			 * 2. else do
			 *        try to move to next sibling
			 *    while (move to parent);
			 *
			 * But we are doing it over type relations, not DIE parent/child relations.
			 * To avoid re-finding our place in data-member and formal-parameter DIE sequences,
			 * we want our stack to record the sequence, somehow, not the position.
			 * I think we can do that just by constructing a subseq_of of the
			 * appropriate type.
			 */
			auto go_deeper = [&]() -> pair<iterator_base, iterator_base> {
				auto NO_DEEPER = make_pair(iterator_base::END, iterator_base::END);
				if (!base() && !!*this) { /* void case */ return NO_DEEPER; }
				else if (base().is_a<type_chain_die>()) // unary case -- includes typedefs, arrays, pointer/reference, ...
				{
					// recursively walk the chain's target
					return make_pair(base().as_a<type_chain_die>()->find_type(), base());
				}
				else if (base().is_a<with_data_members_die>()) 
				{
					// descend to first member child or first inheritance child
					auto data_member_children = base().as_a<with_data_members_die>().children().subseq_of<data_member_die>();
					if (data_member_children.first != data_member_children.second)
					{
						return make_pair(
							data_member_children.first->find_or_create_type_handling_bitfields(),
							/* the reason is the member */
							data_member_children.first
						);
					} else return NO_DEEPER;
				}
				else if (base().is_a<subrange_type_die>())
				{
					// visit the base type
					auto explicit_t = base().as_a<subrange_type_die>()->find_type();
					// HACK: assume this is the same as for enums
					return make_pair(
						explicit_t ? explicit_t : base().enclosing_cu()->implicit_subrange_base_type(),
						base()
					);
				}
				else if (base().is_a<enumeration_type_die>())
				{
					// visit the base type -- HACK: assume subrange base is same as enum's
					auto explicit_t = base().as_a<enumeration_type_die>()->find_type();
					return make_pair(
						explicit_t ? explicit_t : base().enclosing_cu()->implicit_enum_base_type(),
						base()
					);
				}
				else if (base().is_a<type_describing_subprogram_die>())
				{
					/* TRICKY: our "children" consist of firstly a return type,
					 * and later a bunch of formal parameter types. If we've
					 * just visited the return type and want to move to the next
					 * sibling, how do we do that? Neither the type itself nor
					 * the "reason" of the subprogram is sufficient, because
					 * the same type might appear in multiple places in the signature.
					 * AH, it's okay. We can give the return type's "reason" as
					 * the subprogram, and the argument types' "reason" as the FP
					 * DIEs. */
					auto sub_t = base().as_a<type_describing_subprogram_die>();
					return make_pair(sub_t->find_type() /* even if void */,
						sub_t);
				}
				else
				{
					// what are our nullary cases?
					assert(base().is_a<base_type_die>() || base().is_a<unspecified_type_die>());
					return NO_DEEPER;
				}
			};
			auto backtrack = [&]() -> bool {
				assert(!m_stack.empty());
				pair<iterator_base, iterator_base> pos_and_reason = m_stack.back(); m_stack.pop_back();
				this->base_reference() = std::move(pos_and_reason.first);
				// return whether we should keep trying sideways
				return !m_stack.empty();
			};
			auto go_sideways = [&]() -> pair<iterator_base, iterator_base> {
				auto& reason = m_stack.back().second;
				auto NO_MORE = make_pair(iterator_base::END, iterator_base::END);
				assert(m_stack.back().first);
				/* NOTE: we are testing "reason", and it need not be a type.
				 * It just needs to be a thing with a type,
				 * so includes members, inheritances, FPs, ... */
				if (reason.is_a<data_member_die>())
				{
					/* Get the next data member -- just using the iterator_base should be enough. */
					basic_die::children_iterator<data_member_die> cur(m_stack.back().second, END);
					assert(cur);
					++cur;
					if (cur) return make_pair(cur->get_type(), cur);
					return NO_MORE;
				}
				else if (reason.is_a<type_describing_subprogram_die>())
				{
					/* For a subprogram type, the return type's "reason" is
					 * the subprogram, and the argument types' "reason" is the FP
					 * DIE. */
					auto fp_seq = reason.as_a<type_describing_subprogram_die>()->children().subseq_of<formal_parameter_die>();
					auto next = fp_seq.first;
					if (next != fp_seq.second) return make_pair(next->get_type(), next);
					else return NO_MORE;
				}
				else if (reason.is_a<formal_parameter_die>())
				{
					basic_die::children_iterator<formal_parameter_die> cur(m_stack.back().first, END);
					++cur;
					if (cur) return make_pair(cur->get_type(), cur);
					else return NO_MORE;
				}
				else if (reason.is_a<type_chain_die>())
				{
					return NO_MORE;
				}
				else
				{
					// what are our nullary cases?
					assert(!reason
						|| reason.is_a<base_type_die>() || reason.is_a<unspecified_type_die>()
						|| reason.is_a<subrange_type_die>() || reason.is_a<enumeration_type_die>()
						|| reason.is_a<string_type_die>());
					return NO_MORE;
				}
			};
			
			//std::cerr << "Trying to move deeper..." << std::endl;
			pair<iterator_base, iterator_base> deeper_target = go_deeper();
			if (!skip_dependencies && (deeper_target.first || deeper_target.second))
			{
				/* "descend"... but with a catch, because we need to avoid cycles.
				 * Don't descend to something we're already visiting! */
				//std::cerr << "Found deeper (" << deeper_target.first.summary() << "); are we walking it already?..." << std::endl;
				auto matches_target = [&deeper_target](const pair< iterator_base, iterator_base >& p) {
					return p.first == deeper_target.first;
				};
				if (std::find_if(m_stack.begin(), m_stack.end(), matches_target) == m_stack.end())
				{
					//std::cerr << "No, so go deeper..." << std::endl;
					this->m_stack.push_back(deeper_target);
					this->base_reference() = std::move(deeper_target.first);
					return;
				}
				// else we are already walking the deeper thing, so do as if we can't descend
				//std::cerr << "Yes, so pretending we can't move deeper..." << std::endl;
			}
			
			do
			{
				//std::cerr << "Trying to move sideways..." << std::endl;
				auto sideways_target = go_sideways();
				if (sideways_target.first || sideways_target.second)
				{
					//std::cerr << "Found sideways, so moving there..." << std::endl;
					/* replace the top element */
					this->m_stack.pop_back();
					this->m_stack.push_back(sideways_target);
					this->base_reference() = sideways_target.first;
					return;
				}
				//std::cerr << "Nowhere sideways to move... backtracking" << std::endl;
			} while (!m_stack.empty() ? backtrack() : false);
			
			/* We've run out. */
			assert(m_stack.empty());
			*this = iterator_base::END;
			assert(!reason());
		}
		void type_iterator_df::increment_skipping_dependencies()
		{
			return this->increment(true);
		}
		void type_iterator_df::decrement()
		{
			assert(false); // FIXME
		}

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
			const dieloc_set& currently_walking /* = empty */)
		{
			auto key = !t ? opt<Dwarf_Off>() : opt<Dwarf_Off>(t.offset_here());
			if (currently_walking.find(key) != currently_walking.end()) return; // "grey node"
			
			bool continue_recursing;
			if (pre_f) continue_recursing = pre_f(t, reason); // i.e. we do walk "void"
			else continue_recursing = true;
			
			dieloc_set next_currently_walking = currently_walking;
			next_currently_walking.insert(key);
			
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
					// recursively walk all members and inheritances
					auto member_children = t.as_a<with_data_members_die>().children().subseq_of<data_member_die>();
					for (auto i_child = member_children.first;
						i_child != member_children.second; ++i_child)
					{
						walk_type(i_child->find_or_create_type_handling_bitfields(),
							i_child, pre_f, post_f, next_currently_walking);
					}
				}
				else if (t.is_a<subrange_type_die>())
				{
					// visit the base type
					auto explicit_t = t.as_a<subrange_type_die>()->find_type();
					// HACK: assume this is the same as for enums
					walk_type(explicit_t ? explicit_t : t.enclosing_cu()->implicit_subrange_base_type(), t, pre_f, post_f, next_currently_walking);
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
					walk_type(sub_t->find_type(), sub_t, pre_f, post_f, next_currently_walking);
					auto fps = sub_t.children().subseq_of<formal_parameter_die>();
					for (auto i_fp = fps.first; i_fp != fps.second; ++i_fp)
					{
						walk_type(i_fp->find_type(), i_fp, pre_f, post_f, next_currently_walking);
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
		opt<Dwarf_Unsigned> type_die::calculate_byte_size() const
		{
			return get_byte_size(); 
		}
		iterator_df<type_die> type_die::get_concrete_type() const
		{
			return find_self();
		}
		iterator_df<type_die> type_die::get_unqualified_type() const
		{
			return find_self();
		}
		
		opt<uint32_t> type_die::summary_code() const
		{
			debug(2) << "Computing summary code for " << *this << std::endl;
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
			/* if we have it cached, return that */
			if (this->cached_summary_code) return this->cached_summary_code;
			auto found_cached = get_root().type_summary_code_cache.find(get_offset());
			if (found_cached != get_root().type_summary_code_cache.end())
			{
				this->cached_summary_code = found_cached->second;
				return found_cached->second;
			}
			/* helper function that understands the null (void) case */
			//auto type_summary_code = [](core::iterator_df<core::type_die> t) -> opt<uint32_t> {
			//	if (!t) return opt<uint32_t>(0);
			//	else return t->summary_code();
			//};
			auto name_for_type_die = [](core::iterator_df<core::type_die> t) -> opt<string> {
				if (t.is_a<dwarf::core::subprogram_die>())
				{
					/* When interpreted as types, subprograms don't have names. */
					return opt<string>();
				}
				else return *t.name_here();
			};
			opt<uint32_t> code_to_return;
			summary_code_word_t output_word;
			auto outer_t = find_self().as_a<type_die>();
			decltype(outer_t) concrete_outer_t;
			if (!outer_t) { code_to_return = opt<uint32_t>(0); goto out; }
			concrete_outer_t = outer_t->get_concrete_type();
			
			using lib::Dwarf_Unsigned;
			using lib::Dwarf_Half;
			using namespace dwarf::core;
			/* Visit all the DIEs making up the type. At each one,
			 * in the relevant sequence, "<<" in the  */
			walk_type(outer_t, outer_t, /* pre_f */ [&output_word, name_for_type_die](
				iterator_df<type_die> t, iterator_df<program_element_die> reason) {
				debug(2) << "Pre-walking ";
				t.print(debug(2), 0); debug(2) << std::endl;
				// don't want concrete_t to be defined for most of this function...
				{
					auto concrete_t = t ? t->get_concrete_type() : t;
					// non-concrete? ignore but keep going -- walk_type will go down the chain
					// ACTUALLY: when we hit another type, 
					// FIXME: don't continue the walk -- use summary_code() on it, to benefit from caching
					// + ditto on other cases.
					// AH, but "stopping the walk" needs to distinguish skipping a subtree from stopping completely;
					// incrementalise "walk_type" as another kind of iterator? model on iterator_bf.
					// what about getting both pre- and post-order in one? we seem to get that here; use currently_walking?
					if (t != concrete_t) return true;
				}
				// void? our code is 0; keep going
				if (!t) { output_word << 0; return true; }
				// just a decl, no def? try to replace ourselves with the definition, and if not, null out. */
				if (t->get_declaration() && *t->get_declaration())
				{
					iterator_df<> found = t->find_definition();
					t = found.as_a<type_die>();
					if (!t)
					{
						debug(2) << "Detected that we have a declaration with no definition; returning no code" << std::endl;
						// NOTE that we will still get a post-visit, just no recursion
						// so we explicitly clear the output word (HACK)
						output_word.invalidate();
						return false;
					}
				}
				/* If we have a "reason" that is a member_die or inheritance_die, 
				 * */
				if (reason && (reason.is_a<member_die>() || reason.is_a<inheritance_die>()))
				{
					debug(2) << "This type is walked for reason of a member: ";
					reason.print(debug(2), 0);
					debug(2) << std::endl;
					// skip members that are mere declarations 
					if (reason->get_declaration() && *reason->get_declaration()) return false;

					// calculate its offset
					opt<Dwarf_Unsigned> opt_offset = reason.as_a<with_dynamic_location_die>()
						->byte_offset_in_enclosing_type();
					if (!opt_offset)
					{
						debug(2) << "Warning: saw member " << *reason << " with no apparent offset." << endl;
						return false;
					}
					auto member_type = reason.as_a<with_dynamic_location_die>()->get_type();
					assert(member_type);
					assert(member_type.is_a<type_die>());

					output_word << (opt_offset ? *opt_offset : 0);
					// FIXME: also its bit offset!

					// type-visiting logic will do the member type
				}

				Dwarf_Half tag = t.tag_here();
				opt<string> maybe_fq_str = t->get_decl_file() ? t.enclosing_cu()->source_file_fq_pathname(
						*t->get_decl_file()) : opt<string>();
				std::ostringstream tmp;
				string fq_pathname_str = maybe_fq_str 
					? *maybe_fq_str 
					: t->get_decl_file() ? 
						t.enclosing_cu()->source_file_name(*t->get_decl_file())
						: /* okay, give up and use the offset after all */
					(tmp << std::hex << t.offset_here(), tmp.str());

				if (t.is_a<base_type_die>())
				{
					auto base_t = t.as_a<core::base_type_die>();
					unsigned encoding = base_t->get_encoding();
					assert(base_t->get_byte_size());
					unsigned byte_size = *base_t->get_byte_size();
					unsigned bit_size = base_t->get_bit_size() ? *base_t->get_bit_size() : byte_size * 8;
					unsigned bit_offset = base_t->get_bit_offset() ? *base_t->get_bit_offset() : 0;
					output_word << DW_TAG_base_type << encoding << byte_size << bit_size << bit_offset;
				} 
				else if (t.is_a<enumeration_type_die>())
				{
					// shift in the enumeration name
					if (t.name_here())
					{
						output_word << *name_for_type_die(t);
					} else output_word << std::hash<string>()(fq_pathname_str);

					// shift in the names and values of each enumerator
					auto enum_t = t.as_a<enumeration_type_die>();
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

					// walk_type will shift in the base type's summary code
				}
				else if (t.is_a<subrange_type_die>())
				{
					auto subrange_t = t.as_a<subrange_type_die>();

					// shift in the name, if any
					if (t.name_here())
					{
						output_word << *name_for_type_die(t);
					} else output_word << std::hash<string>()(fq_pathname_str);

					// walk_type will shift in the base type's summary code
					// post_f will shift in the upper bound and lower bound, if present
				} 
				else if (t.is_a<type_describing_subprogram_die>())
				{
					auto subp_t = t.as_a<type_describing_subprogram_die>();

					// first, shift in something to distinguish void(void) from void
					output_word << "()";

					// walk_type will shift in the argument and return types

					// post_f will deal with variadics
				}
				else if (t.is_a<address_holding_type_die>())
				{
					/* Danger: recursive data types! But walk_type deals with this. */
					auto ptr_t = t.as_a<core::address_holding_type_die>();
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
					output_word << tag << ptr_size << addr_class;

					// walk_type deals with the target type
				}
				else if (t.is_a<with_data_members_die>())
				{
					// add in the name if we have it
					if (t.name_here())
					{
						output_word << *name_for_type_die(t);
					} else output_word << std::hash<string>()(fq_pathname_str);

					// for each member -- walk_type does that, and we do the member offset at the top of pre_f
				}
				else if (t.is_a<array_type_die>())
				{
					// nothing here -- it's in post_f
				}
				else if (t.is_a<string_type_die>())
				{
					// Fortran strings can be fixed-length or variable-length
					auto opt_dynamic_length = t.as_a<string_type_die>()->get_string_length();
					unsigned byte_len;
					if (opt_dynamic_length) byte_len = 0; /* treat as length 0 */ 
					else
					{
						auto opt_byte_size = t.as_a<string_type_die>()->fixed_length_in_bytes();
						assert(opt_byte_size);
						byte_len = *opt_byte_size;
					}
					output_word << DW_TAG_string_type << byte_len;
				}
				else if (t.is_a<unspecified_type_die>())
				{
					debug(2) << "Warning: saw unspecified type " << t;
					output_word.val = opt<uint32_t>();
				}
				else 
				{
					debug(2) << "Warning: didn't understand type " << t;
				}
				return true;
			},
			/* post_f */ [&output_word](
				iterator_df<type_die> t, iterator_df<program_element_die> reason) -> void {
				debug(2) << "Post-walking ";
				t.print(debug(2), 0); debug(2) << std::endl;
				if (t && t != t->get_concrete_type()) return;
				if (t.is_a<type_describing_subprogram_die>())
				{
					auto subp_t = t.as_a<type_describing_subprogram_die>();
					// distinguish with variadics
					if (subp_t->is_variadic()) output_word << "...";
				}
				else if (t.is_a<subrange_type_die>())
				{
					auto subrange_t = t.as_a<subrange_type_die>();

					/* Then shift in the upper bound and lower bound, if present
					 * NOTE: this means unnamed boundless subrange types have the 
					 * same code as their underlying type. This is probably what we want. */
					if (subrange_t->get_upper_bound()) output_word << *subrange_t->get_upper_bound();
					if (subrange_t->get_lower_bound()) output_word << *subrange_t->get_lower_bound();
				}
				else if (t.is_a<array_type_die>())
				{
					// walk_type has done the element type
					
					// if we're a member of something, we should be bounded in all dimensions
					auto opt_el_count = t.as_a<array_type_die>()->ultimate_element_count();
					output_word << (opt_el_count ? *opt_el_count : 0);
					// FIXME: also the factoring into dimensions needs to be taken into account
				}
				// pointer-to-incomplete, etc., will still give us incomplete answer
				assert (!t || !(output_word.val) || *output_word.val != 0);
			}
			); /* end call to walk_type */
			debug(2) << "Finished walk" << std::endl;
			code_to_return = output_word.val; 
			out:
				get_root().type_summary_code_cache.insert(
					make_pair(get_offset(), code_to_return)
				);
			debug(2) << "Got summary code: ";
			if (code_to_return) debug(2) << std::hex << *code_to_return << std::dec;
			else debug(2) << "(no code)";
			debug(2) << endl;
			this->cached_summary_code = code_to_return;
			return code_to_return;
		}
		bool type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			
			debug(2) << "Testing type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			return t.tag_here() == get_tag(); // will be refined in subclasses
		}
		bool type_die::equal(iterator_df<type_die> t, 
			const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal
			) const
		{
			set<pair< iterator_df<type_die>, iterator_df<type_die> > > flipped_set;
			auto& r = get_root();
			auto self = find_self();
			
			// iterator equality always implies type equality
			if (self == t) return true;
			
			if (assuming_equal.find(make_pair(self, t)) != assuming_equal.end())
			{
				return true;
			}
			/* If the two iterators share a root, check the cache */
			if (t && &t.root() == &self.root())
			{
				auto found_seq = self.root().equal_to.equal_range(t.offset_here());
				for (auto i_found = found_seq.first; i_found != found_seq.second; ++i_found)
				{
					if (i_found->second.first == self.offset_here())
					{
						return i_found->second.second;
					}
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
			
			t_may_equal_self = t->may_equal(self, flipped_set);
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
		{ return equal(get_root().find(t.get_offset()), {}); }
/* from base_type_die */
		bool base_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
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
		opt<Dwarf_Unsigned> base_type_die::calculate_byte_size() const
		{
			/* DWARF4 allows us to have bit_size but not byte_size. */
			auto opt_byte_size = get_byte_size();
			if (opt_byte_size) return opt_byte_size;
			auto opt_bit_size = get_bit_size();
			if (opt_bit_size) return opt<Dwarf_Unsigned>(
				(*opt_bit_size % 8 == 0) ? *opt_bit_size / 8 : 1 + *opt_bit_size / 8
			);
			return opt<Dwarf_Unsigned>();
		}
		pair<Dwarf_Unsigned, Dwarf_Unsigned> base_type_die::bit_size_and_offset() const
		{
			opt<Dwarf_Unsigned> opt_bsz = get_bit_size();
			opt<Dwarf_Unsigned> opt_boff = get_bit_offset();
			opt<Dwarf_Unsigned> opt_data_boff = get_data_bit_offset();
			
			unsigned bit_size = opt_bsz ? *opt_bsz : 8 * *calculate_byte_size();
			unsigned bit_offset = opt_data_boff ? *opt_data_boff : 
				opt_boff ? *opt_boff : 0;
			
			return make_pair(bit_size, bit_offset);
		}
		bool base_type_die::is_bitfield_type() const
		{
			auto opt_byte_sz = this->calculate_byte_size();
			pair<Dwarf_Unsigned, Dwarf_Unsigned> bit_size_and_offset = this->bit_size_and_offset();
			return !(bit_size_and_offset.second == 0
				&& bit_size_and_offset.first == 8 * *opt_byte_sz);
		}
/* from array_type_die */
		iterator_df<type_die> array_type_die::get_concrete_type() const
		{
			return find_self();
		}
		bool array_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			
			debug(2) << "Testing array_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
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
/* from string_type_die */
		bool string_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			
			debug(2) << "Testing string_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			if (get_name() != t.name_here()) return false;

			// our has-dynamic-lengthness should be equal
			bool dynamic_lengthness_equal = (t.as_a<string_type_die>()->get_string_length()
				== get_string_length());
			if (!dynamic_lengthness_equal) return false;
			// if we don't have dynamic length, any static length should be equal
			if (!get_string_length())
			{
				auto our_opt_byte_size = get_byte_size();
				auto other_opt_byte_size = t.as_a<string_type_die>()->get_byte_size();
				if (our_opt_byte_size != other_opt_byte_size) return false;
			}
			return true;
		}
		opt<Dwarf_Unsigned> string_type_die::fixed_length_in_bytes() const
		{
			if (this->get_string_length()) return opt<Dwarf_Unsigned>();
			return this->get_byte_size();
		}
		opt<Dwarf_Unsigned> string_type_die::calculate_byte_size() const
		{
			return this->fixed_length_in_bytes();
		}
		opt<encap::loclist> string_type_die::dynamic_length_in_bytes() const
		{
			auto opt_string_length = this->get_string_length();
			if (!opt_string_length) return opt<encap::loclist>();
			return *opt_string_length;
		}
/* from subrange_type_die */
		bool subrange_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			debug(2) << "Testing subrange_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
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
		bool enumeration_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			debug(2) << "Testing enumeration_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
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
		iterator_df<type_die> qualified_type_die::get_unqualified_type() const
		{
			// for qualified types, our unqualified self is our get_type, recursively unqualified
			opt<iterator_df<type_die> > opt_next_type = get_type();
			if (!opt_next_type) return iterator_base::END; 
			if (!opt_next_type.is_a<qualified_type_die>()) return opt_next_type;
			else return iterator_df<qualified_type_die>(std::move(opt_next_type))
				->get_unqualified_type();
		} 
/* from spec::type_chain_die */
		opt<Dwarf_Unsigned> type_chain_die::calculate_byte_size() const
		{
			// Size of a type_chain is always the size of its concrete type
			// which is *not* to be confused with its pointed-to type!
			auto next_type = get_concrete_type();
			if (get_offset() == next_type.offset_here())
			{
				assert(false); // we're too generic to know our byte size; should have hit a different overload
			}
			else if (next_type != iterator_base::END)
			{
				auto to_return = next_type->calculate_byte_size();
				if (!to_return)
				{
					debug(2) << "Type chain concrete type " << *get_concrete_type()
						<< " returned no byte size" << endl;
				}
				return to_return;
			}
			else
			{
				debug(2) << "Type with no concrete type: " << *this << endl;
				return opt<Dwarf_Unsigned>();
			}
		}
		bool type_chain_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			debug(2) << "Testing type_chain_die::may_equal() (default case)" << endl;
			
			return get_tag() == t.tag_here() && 
				(
					(!get_type() && !t.as_a<type_chain_die>()->get_type())
				||  (get_type() && t.as_a<type_chain_die>()->get_type() && 
					get_type()->equal(t.as_a<type_chain_die>()->get_type(), assuming_equal)));
		}
		iterator_df<type_die> type_chain_die::get_concrete_type() const
		{
			// pointer and reference *must* override us -- they do not follow chain
			assert(get_tag() != DW_TAG_pointer_type
				&& get_tag() != DW_TAG_reference_type
				&& get_tag() != DW_TAG_rvalue_reference_type
				&& get_tag() != DW_TAG_array_type);
			
			root_die& r = get_root(); 
			auto opt_next_type = get_type();
			if (!opt_next_type) return iterator_base::END; // a.k.a. None
			if (!get_spec(r).tag_is_type(opt_next_type.tag_here()))
			{
				debug(2) << "Warning: following type chain found non-type " << opt_next_type << endl;
				return find_self();
			} 
			else return opt_next_type->get_concrete_type();
		}
/* from spec::address_holding_type_die */  
		iterator_df<type_die> address_holding_type_die::get_concrete_type() const 
		{
			return find_self();
		}
		opt<Dwarf_Unsigned> address_holding_type_die::calculate_byte_size() const 
		{
			root_die& r = get_root();
			auto opt_size = get_byte_size();
			if (opt_size) return opt_size;
			else return r.cu_pos(get_enclosing_cu_offset())->get_address_size();
		}
/* from spec::array_type_die */
		opt<Dwarf_Unsigned> array_type_die::element_count() const
		{
			auto all_counts = this->dimension_element_counts();
			opt<Dwarf_Unsigned> opt_total_count;
			
			for (auto i_count = all_counts.begin(); i_count != all_counts.end(); ++i_count)
			{
				if (*i_count) 
				{
					opt_total_count = opt_total_count ? 
						*opt_total_count * (**i_count)
						: (*i_count);
				}
				else
				{
					/* We have a subrange with no bounds. So we have no overall count. */
					opt_total_count = opt<Dwarf_Unsigned>();
					break;
				}
			}
			
			return opt_total_count;
		}
		
		vector< opt<Dwarf_Unsigned> > array_type_die::dimension_element_counts() const
		{
			auto element_type = get_type();
			assert(element_type != iterator_base::END);
			vector< opt<Dwarf_Unsigned> > all_counts;

			root_die& r = get_root();
			auto self = find_self();
			assert(self != iterator_base::END);

			auto enclosing_cu = r.cu_pos(get_enclosing_cu_offset());
			auto opt_implicit_lower_bound = enclosing_cu->implicit_array_base();
			
			auto subrs = self.children_here().subseq_of<subrange_type_die>();
			for (auto i_subr = std::move(subrs.first); i_subr != subrs.second; ++i_subr)
			{
				/* The subrange might come with a "count" or upper/lower bounds. */
				auto opt_this_subr_count = i_subr->get_count();
				if (!opt_this_subr_count)
				{
					auto opt_lower_bound = i_subr->get_lower_bound();
					if (!opt_lower_bound && !opt_implicit_lower_bound)
					{
						/* do nothing -- we have no count */
					}
					else
					{
						Dwarf_Unsigned lower_bound;
						if (opt_lower_bound) lower_bound = *opt_lower_bound;
						else if (opt_implicit_lower_bound) lower_bound = *opt_implicit_lower_bound;
						else assert(0);
						
						opt<Dwarf_Unsigned> opt_upper_bound = i_subr->get_upper_bound();
						if (opt_upper_bound) 
						{
							Dwarf_Unsigned upper_bound = *opt_upper_bound;
							opt_this_subr_count = opt<Dwarf_Unsigned>(upper_bound - lower_bound + 1);
						}
						else
						{
							/* again, do nothing  */
						}
					}
				}
				
				all_counts.push_back(opt_this_subr_count);
			}
			return all_counts;
		}

		opt<Dwarf_Unsigned> array_type_die::calculate_byte_size() const
		{
			auto element_type = get_type();
			assert(element_type != iterator_base::END);
			opt<Dwarf_Unsigned> count = element_count();
			opt<Dwarf_Unsigned> calculated_byte_size = element_type->calculate_byte_size();
			if (count && calculated_byte_size) return *count * *calculated_byte_size;
			else return opt<Dwarf_Unsigned>();
		}
		
		iterator_df<type_die> array_type_die::ultimate_element_type() const
		{
			iterator_df<type_die> cur = get_concrete_type();
			while (cur != iterator_base::END
				 && cur.is_a<array_type_die>())
			{
				cur = cur.as_a<array_type_die>()->get_type();
				if (cur != iterator_base::END) cur = cur->get_concrete_type();
			}
			return cur;
		}
		
		opt<Dwarf_Unsigned> array_type_die::ultimate_element_count() const 
		{
			Dwarf_Unsigned count = 1;
			iterator_df<type_die> cur = get_concrete_type();
			while (cur != iterator_base::END
				 && cur.is_a<array_type_die>())
			{
				auto opt_count = cur.as_a<array_type_die>()->element_count();
				if (!opt_count) return opt<Dwarf_Unsigned>();
				else 
				{
					count *= *opt_count;
					cur = cur.as_a<type_chain_die>()->get_type();
					if (cur != iterator_base::END) cur = cur->get_concrete_type();
				}
			}
			return opt<Dwarf_Unsigned>(count);
		}
		
		
/* from spec::structure_type_die */
		opt<Dwarf_Unsigned> structure_type_die::calculate_byte_size() const
		{
			// HACK: for now, do nothing
			// We should make this more reliable,
			// but it's difficult because overall size of a struct is
			// language- and implementation-dependent.
			return this->type_die::calculate_byte_size();
		}
/* from spec::with_data_members_die */
		bool with_data_members_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			debug(2) << "Testing with_data_members_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
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
					find_self().as_a<type_die>(),
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
		iterator_base with_data_members_die::find_definition() const
		{
			root_die& r = get_root();
			if (!get_declaration() || !*get_declaration()) 
			{
				/* we are a definition already */
				return find_self();
			}
			debug(2) << "Looking for definition of declaration " << summary() << endl;
			
			// if we don't have a name, we have no way to proceed
			auto opt_name = get_name(/*r*/);
			if (!opt_name) goto return_no_result;
			else
			{
				string my_name = *opt_name;

				/* Otherwise, we search forwards from our position, for siblings
				 * that have the same name but no "declaration" attribute. */
				auto iter = find_self();

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
						&& (opt_decl_flag = i_sib->get_declaration(), 
							!opt_decl_flag || !*opt_decl_flag))
					{
						debug(2) << "Found definition " << i_sib->summary() << endl;
						return i_sib;
					}
				}
			}
		return_no_result:
			debug(2) << "Failed to find definition of declaration " << summary() << endl;
			return iterator_base::END;
		}

		bool variable_die::has_static_storage() const
		{
			// don't bother testing whether we have an enclosing subprogram -- too expensive
			//if (nonconst_this->nearest_enclosing(DW_TAG_subprogram))
			//{
				// we're either a local or a static -- skip if local
				root_die& r = get_root();
				auto attrs = copy_attrs();
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
		with_dynamic_location_die::byte_offset_in_enclosing_type(
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
			
			root_die& r = get_root();
			auto it = find_self();
			auto parent = r.parent(it);
			auto enclosing_type_die = parent.as_a<core::type_die>();
			if (!enclosing_type_die) return opt<Dwarf_Unsigned>();
			
			opt<encap::loclist> data_member_location 
			 = it.is_a<member_die>() ? it.as_a<member_die>()->get_data_member_location() 
			 : it.is_a<inheritance_die>() ? it.as_a<inheritance_die>()->get_data_member_location()
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
				assert(parent_first_member != iterator_base::END);
				while (
					!(parent_first_member.is_a<member_die>() || parent_first_member.is_a<inheritance_die>())
					|| (parent_first_member->get_declaration() && *parent_first_member->get_declaration())
				)
				{
					++parent_first_member;
					// at the latest, we should hit ourselves
					assert(parent_first_member != iterator_base::END);
				}
				
				// if we are the first member of a struct, or any member of a union, we're okay
				if (it.offset_here() == parent_first_member.offset_here()
				 || enclosing_type_die.tag_here() == DW_TAG_union_type)
				{
					return opt<Dwarf_Unsigned>(0U);
				}
				
				/* Otherwise we might still be okay. FIXME: have I really seen code that needs this? */
				if (assume_packed_if_no_location)
				{
					auto previous_member = parent_first_member;
					assert(previous_member);
					// if there is one member or more before us...
					if (previous_member != it)
					{
						do
						{
							auto next_member = previous_member;
							// advance to the next non-decl member or inheritance DIE 
							do
							{
								++next_member;
							} while (next_member != it
								&& (!(next_member.is_a<member_die>() || next_member.is_a<inheritance_die>())
								|| (next_member->get_declaration() && *next_member->get_declaration())));
							// break if we hit ourselves
							if (next_member == it) break;
							previous_member = std::move(next_member);
						} while (true); 

						if (previous_member) 
						{
							auto prev_memb_t = previous_member->get_type();
							if (prev_memb_t)
							{
								auto opt_prev_byte_size = prev_memb_t->calculate_byte_size();
								if (opt_prev_byte_size)
								{
									/* Do we have an offset for the previous member? */
									auto opt_prev_member_offset = previous_member->byte_offset_in_enclosing_type(
										true);

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
				debug() << "Warning: encountered DWARF member lacking a location: "
					<< it << std::endl;
				return opt<Dwarf_Unsigned>();
			}
			else if (data_member_location->size() != 1)
			{
				debug() << "Bad location: " << *data_member_location << std::endl;
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
					return expr::evaluator(
						data_member_location->at(0), 
						it.enclosing_cu().spec_here(), 
						std::stack<Dwarf_Unsigned>(std::deque<Dwarf_Unsigned>(1, 0UL))).tos();
				} 
				catch (expr::Not_supported)
				{
					goto location_not_understood;
				}
			}
		location_not_understood:
				// error
				debug() << "Warning: encountered DWARF member with location I didn't understand: "
					<< it << std::endl;
				return opt<Dwarf_Unsigned>();
		}
		
		boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned> 
		with_static_location_die::file_relative_intervals(
		
			root_die& r, 
		
			sym_binding_t (*sym_resolve)(const std::string& sym, void *arg), 
			void *arg /* = 0 */) const
		{
			encap::attribute_map attrs = d.copy_attrs();
			
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

					opt<Dwarf_Unsigned> opt_byte_size;
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
							auto calculated_byte_size = t->calculate_byte_size();
							assert(calculated_byte_size);
							opt_byte_size = *calculated_byte_size; // assign to *another* opt
						}
					}
					assert(opt_byte_size);
					Dwarf_Unsigned byte_size = *opt_byte_size;
					if (byte_size == 0)
					{
						debug(2) << "Zero-length object: " << summary() << endl;
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
							debug(2) << "Vaddr-dependent static location " << *this << endl;
						}
						else debug(2) << "Static var with no location: " << *this << endl;
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
							Dwarf_Unsigned piece_start = expr::evaluator(i->first,
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
					catch (expr::Not_supported)
					{
						// some opcode we don't recognise
						debug() << "Unrecognised opcode in " << *this << endl;
						goto out;
					}

				}
				else if (sym_resolve &&
					found_linkage_name != attrs.end())
				{
					std::string linkage_name = found_linkage_name->second.get_string();

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
						debug() << "Warning: couldn't resolve linkage name " << linkage_name
							<< " for DIE " << *this << std::endl;
					}
				}

			}
		out:
			//debug() << "Intervals of " << this->summary() << ": " << retval << endl;
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
		encap::loclist with_static_location_die::get_static_location() const
        {
        	auto attrs = copy_attrs();
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
					expr::regs *p_regs/* = 0*/) const
		{
			typedef opt< pair<Dwarf_Off, iterator_df<with_dynamic_location_die> > >
				return_type;
			
			/* auto nonconst_this = const_cast<subprogram_die *>(this); */
			assert(this->get_frame_base());
			auto i = find_self();
			assert(i != iterator_base::END);
			
			// Calculate the vaddr which selects a loclist element
			auto frame_base_loclist = *get_frame_base();
			iterator_df<compile_unit_die> enclosing_cu
			 = r.cu_pos(i.enclosing_cu_offset_here());
			debug(2) << "Enclosing CU is " << enclosing_cu->summary() << endl;
			Dwarf_Addr low_pc = enclosing_cu->get_low_pc()->addr;
			assert(low_pc <= dieset_relative_ip);
			Dwarf_Addr vaddr = dieset_relative_ip - low_pc;
			/* Now calculate our frame base address. */
			auto frame_base_addr = expr::evaluator(
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
			frame_subobject_iterator start_iter(child);
			debug(2) << "Exploring stack-located children of " << summary() << std::endl;
			unsigned initial_depth = start_iter.depth();
			for (auto i_bfs = start_iter;
					i_bfs.depth() >= initial_depth;
					++i_bfs)
			{
				debug(2) << "Considering whether DIE has stack location: " 
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
		iterator_df<type_die> subprogram_die::get_return_type() const
		{
			return get_type(); 
		}
/* from type_describing_subprogram_die */
		bool type_describing_subprogram_die::is_variadic() const
		{
			auto i = find_self();
			assert(i != iterator_base::END);
			auto children = i.children_here();
			auto unspec = children.subseq_of<unspecified_parameters_die>();
			return unspec.first != unspec.second;
		}
		bool type_describing_subprogram_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			debug(2) << "Testing type_describing_subprogram_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
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
		iterator_df<type_die> subroutine_type_die::get_return_type() const
		{
			return get_type();
		}
/* from spec::with_dynamic_location_die */
		iterator_df<program_element_die> 
		with_dynamic_location_die::get_instantiating_definition() const
		{
			/* We want to return a parent DIE describing the thing whose instances
			 * contain instances of us. 
			 *
			 * If we're a member or inheritance, it's our nearest enclosing type.
			 * If we're a variable or fp, it's our enclosing subprogram.
			 * This might be null if we're actually a static variable. */

			auto i = find_self();
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
					expr::regs *p_regs) const
		{
			auto attrs = copy_attrs();
			if (attrs.find(DW_AT_location) == attrs.end())
			{
				debug(2) << "Warning: " << this->summary() << " has no DW_AT_location; "
					<< "assuming it does not cover any stack locations." << endl;
				return opt<Dwarf_Off>();
			}
			auto base_addr = calculate_addr_on_stack(
				frame_base_addr,
				r, 
				dieset_relative_ip,
				p_regs);
			debug(2) << "Calculated that an instance of DIE" << summary()
				<< " has base addr 0x" << std::hex << base_addr << std::dec;
			assert(attrs.find(DW_AT_type) != attrs.end());
			auto size = *(attrs.find(DW_AT_type)->second.get_refiter_is_type()->calculate_byte_size());
			debug(2) << " and size " << size
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
		encap::loclist formal_parameter_die::get_dynamic_location() const
		{
			/* These guys are probably relative to a frame base. 
			   If they're not, it's an error. So we rewrite the loclist
			   so that it's relative to a frame base. */
			
			// see note in expr.hpp
			if (!this->get_location()) return encap::loclist::NO_LOCATION; //(/*encap::loc_expr::NO_LOCATION*/);
			return absolute_loclist_to_additive_loclist(
				*this->get_location());
		
		}
		encap::loclist variable_die::get_dynamic_location() const
		{
			// see note in expr.hpp
			if (!this->get_location()) return encap::loclist::NO_LOCATION; //(/*encap::loc_expr::NO_LOCATION*/);
			
			root_die& r = get_root();
			auto i = find_self();
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
					expr::regs *p_regs) const
		{
			auto attrs = copy_attrs();
			auto base_addr = calculate_addr_in_object(
				object_base_addr, r, dieset_relative_ip, p_regs);
			assert(attrs.find(DW_AT_type) != attrs.end());
			auto size = *(attrs.find(DW_AT_type)->second.get_refiter_is_type()->calculate_byte_size());
			if (absolute_addr >= base_addr
			&&  absolute_addr < base_addr + size)
			{
				return absolute_addr - base_addr;
			}
			return opt<Dwarf_Off>();
		}
		
		/* We want to iterate over visible named grandchildren, in a way that
		 * exploits the cache in resolve_all_visible_from_root. How to do this?
		 *
		 * I think we have to write a new kind of iterator -- visible_named_grandchildren_iterator.
		 * Oh, but that's not quite enough, and doesn't exploit the cache.
		 * How would it work? 
		 * We walk the visible named grandchildren and look for one that matches,
		 * preferentially using the cache.
		 * But the cache is by name.
		 * We could maintain a cache by offset too.
		 * But walking over visible named grandchildren is too easy... we just
		 * do a filter_iterator over grandchildren, since
		 * walking in offset order should be fast anyway.
		 * Okay -- if it's that easy, add it!
		 * 
		 * But also add another kind of iterator: 
		 * a *name-specific* visible-named-grandchildren iterator.
		 * If the cache is complete, we just iterate over the cache.
		 * If it's not complete, we do the naive thing, but ensure that
		 * when we hit the end, we mark the cache as complete. Can we do this?
		 * GAH. Polymorphism on return: is there any easy way to do this?
		 * Can we return a shared ptr to a sequence of some vtable'd type?
		 * NO because the return type of the vtable methods still needs to be fixed.
		 * Iterators need to be passed around by value.
		 * We could easily make our own vtable'd "late-bound iterator" abstract class,
		 * and use that. Hmpf.
		 * */
		
		
/* from data_member_die, special helper for bitfields */
		iterator_df<type_die> data_member_die::find_or_create_type_handling_bitfields() const
		{
			return find_type();
		}
		iterator_df<type_die> member_die::find_or_create_type_handling_bitfields() const
		{
			/* Just do find_type if we don't have the bitfield attributes. */
			if (!get_bit_size() && !get_bit_offset() && !get_data_bit_offset()) return find_type();

			debug(2) << "Handling bitfield member at 0x" << std::hex << get_offset() << std::dec
				<< std::endl;
			
			opt<Dwarf_Unsigned> opt_bsz = get_bit_size();
			opt<Dwarf_Unsigned> opt_boff = get_bit_offset();
			opt<Dwarf_Unsigned> opt_data_boff = get_data_bit_offset();
			iterator_df<type_die> t = find_type();
			
			if (!t.is_a<base_type_die>()) return t;
			auto bt = t.as_a<base_type_die>();
			
			pair<Dwarf_Unsigned, Dwarf_Unsigned> bt_bit_size_and_offset = bt->bit_size_and_offset();
			
			if (!bt->get_name()) return t;
			
			Dwarf_Unsigned effective_bit_size = opt_bsz ? *opt_bsz : bt_bit_size_and_offset.first;
			Dwarf_Unsigned effective_bit_offset = opt_data_boff ? *opt_data_boff : 
				opt_boff ? *opt_boff : bt_bit_size_and_offset.second;
			
			/* Decide whether to create -- need to search existing base types.
			 * HACK: use naming and visible_named_grandchildren:
			 * Varied-width/size types should always have the same name as the full-width one.
			 */
			vector<iterator_base> all_found = get_root().find_all_visible_named_grandchildren(
				*bt->get_name());
				
			/* We want to iterate over visible named grandchildren, in a way that
			 * exploits the cache in resolve_all_visible_from_root. How to do this?
			 * I think we have to write a new kind of iterator. */
			
			/* Does any of them really match? */
			auto not_equal = [effective_bit_size, effective_bit_offset](const iterator_base &i) {
				if (!i.is_a<base_type_die>()) return true;
				auto other_bt = i.as_a<base_type_die>();
				pair<Dwarf_Unsigned, Dwarf_Unsigned> bit_size_and_offset = other_bt->bit_size_and_offset();
				
				return !(effective_bit_size == bit_size_and_offset.first
					&& effective_bit_offset == bit_size_and_offset.second);
			};
			
			Dwarf_Unsigned effective_byte_size = 
				((effective_bit_offset + effective_bit_size) % 8 == 0) ?
				(effective_bit_offset + effective_bit_size) / 8 :
				1 + (effective_bit_offset + effective_bit_size) / 8;
			
			auto new_end = std::remove_if(all_found.begin(), all_found.end(), not_equal);
			switch (srk31::count(all_found.begin(), new_end))
			{
				case 0: {
					// we need to create
					// where to create -- new CU? yes, I guess so
					auto cu = get_root().get_or_create_synthetic_cu();
					auto created = get_root().make_new(cu, DW_TAG_base_type);
					auto& attrs = dynamic_cast<core::in_memory_abstract_die&>(created.dereference())
						.attrs();
					encap::attribute_value v_name(*bt->get_name()); // must have a name
					attrs.insert(make_pair(DW_AT_name, v_name));
					encap::attribute_value v_bit_size(effective_bit_size);
					attrs.insert(make_pair(DW_AT_bit_size, v_bit_size));
					encap::attribute_value v_bit_offset(effective_bit_offset);
					attrs.insert(make_pair(DW_AT_data_bit_offset, v_bit_offset));
					encap::attribute_value v_encoding(bt->get_encoding());
					attrs.insert(make_pair(DW_AT_encoding, v_encoding));
					encap::attribute_value v_byte_size(effective_byte_size);
					attrs.insert(make_pair(DW_AT_byte_size, v_byte_size));
					// debugging
					get_root().print_tree(std::move(cu), debug(2));
					return created;
				}
				default:
					// just go with the first, so fall through
				case 1:
					// pick the first and return
					return all_found.begin()->as_a<base_type_die>();
			}
			
		}
/* from data_member_die */
		encap::loclist data_member_die::get_dynamic_location() const
		{
			/* These guys have loclists that add to what's on the
			   top-of-stack, which is what we want. */
			opt<encap::loclist> opt_location = get_data_member_location();
			return opt_location ? *opt_location : encap::loclist();
		}
/* from spec::with_dynamic_location_die */
		Dwarf_Addr 
		with_dynamic_location_die::calculate_addr_on_stack(
				Dwarf_Addr frame_base_addr,
				root_die& r, 
				Dwarf_Off dieset_relative_ip,
				expr::regs *p_regs/* = 0*/) const
		{
			auto attrs = copy_attrs();
			assert(attrs.find(DW_AT_location) != attrs.end());
			
			/* We have to find ourselves. Well, almost -- enclosing CU. */
			auto found = r.cu_pos(get_enclosing_cu_offset());
			iterator_df<compile_unit_die> i_cu = found;
			assert(i_cu != iterator_base::END);
			Dwarf_Addr dieset_relative_cu_base_ip
			 = i_cu->get_low_pc() ? i_cu->get_low_pc()->addr : 0;
			
			if (dieset_relative_ip < dieset_relative_cu_base_ip)
			{
				debug(2) << "Warning: bad relative IP (0x" << std::hex << dieset_relative_ip << std::dec
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
			debug(2) << "After rewriting, loclist is " << rewritten_loclist << endl;
			
			return (Dwarf_Addr) expr::evaluator(
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
				expr::regs *p_regs /*= 0*/) const
		{
			auto attrs = copy_attrs();
			iterator_df<compile_unit_die> i_cu = r.cu_pos(get_enclosing_cu_offset());
			assert(attrs.find(DW_AT_data_member_location) != attrs.end());
			return (Dwarf_Addr) expr::evaluator(
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
		compile_unit_die::normalize_rangelist(const encap::rangelist& rangelist) const
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

		opt<Dwarf_Unsigned> compile_unit_die::implicit_array_base() const
		{
			switch(get_language())
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
		iterator_df<type_die> compile_unit_die::implicit_enum_base_type() const
		{
			if (cached_implicit_enum_base_type) return cached_implicit_enum_base_type; // FIXME: cache "not found" result too
			/* Language-specific knowledge. */
			switch(get_language())
			{
				case DW_LANG_C:
				case DW_LANG_C89:
				case DW_LANG_C_plus_plus:
				case DW_LANG_C99: {
					const char *attempts[] = { "signed int", "int" };
					size_t total_attempts = sizeof attempts / sizeof attempts[0];
					for (unsigned i_attempt = 0; i_attempt < total_attempts; ++i_attempt)
					{
						auto found = named_child(attempts[i_attempt]);
						if (found != iterator_base::END && found.is_a<type_die>())
						{
							cached_implicit_enum_base_type = found.as_a<type_die>();
							return found;
						}
					}
					assert(false && "enum but no int or signed int");
				}
				default:
					break;
			}
			// FIXME: anything left to do here?
			return iterator_base::END;
		}
		iterator_df<type_die> compile_unit_die::implicit_subrange_base_type() const
		{
			if (cached_implicit_subrange_base_type) return cached_implicit_subrange_base_type; // FIXME: cache "not found" result too

			/* Subranges might have no type, at least in that I've seen gcc generate them.
			 * This does not necessarily mean they are the empty subrange!
			 * Could be a gcc bug, but we need to do something.
			 * Since these subranges are under array types, and because array
			 * ranges are unsigned, we relax a bit and look for unsigneds too.
			 * BUT problem: it need not have the C name for unsigned int.
			 * We used to look for "sizetype" but turns out that's a gcc bug.
			 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80263
			 * Instead, look first for an unnamed unsigned integer type,
			 * then for "sizetype" as a bug workaround,
			 * then whatever we do for enums.
			 */
			switch(get_language())
			{
				case DW_LANG_C:
				case DW_LANG_C89:
				case DW_LANG_C_plus_plus:
				case DW_LANG_C99: {
					const char *attempts[] = { "unsigned int", "unsigned" };
					size_t total_attempts = sizeof attempts / sizeof attempts[0];
					for (unsigned i_attempt = 0; i_attempt < total_attempts; ++i_attempt)
					{
						auto found = named_child(attempts[i_attempt]);
						if (found != iterator_base::END && found.is_a<type_die>())
						{
							cached_implicit_subrange_base_type = found.as_a<type_die>();
							return found;
						}
					}
				}
				default:
					break;
			}
			auto child_bts = children().subseq_of<base_type_die>();
			for (auto i_bt = std::move(child_bts.first); i_bt != child_bts.second; ++i_bt)
			{
				if (i_bt->get_encoding() == DW_ATE_unsigned
					 && (!i_bt.name_here()
						|| /* gcc bug workaround */ *i_bt.name_here() == "sizetype"))
				{
					cached_implicit_subrange_base_type = i_bt.as_a<type_die>();
					return cached_implicit_subrange_base_type;
				}
			}
			return this->implicit_enum_base_type();
		}
	}
} // end namespace dwarf
