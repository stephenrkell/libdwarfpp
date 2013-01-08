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

#include <sstream>
#include <libelf.h>

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

		/* print a single DIE */
		std::ostream& operator<<(std::ostream& s, const basic_die& d)
		{
			s << "DIE, payload tbc"; 
			return s;
		}
		/* print a whole tree of DIEs */
		std::ostream& operator<<(std::ostream& s, const root_die& d)
		{
			for (auto i = d.begin(); i != d.end(); ++i)
			{
				for (unsigned u = 0u; u < i.depth(); ++u) s << '\t';
				s << i;
			}
			return s;
		}
		
		std::ostream& operator<<(std::ostream& s, const iterator_base& i)
		{
			s << "At 0x" << std::hex << i.offset_here() << std::dec
						<< ", depth " << i.depth()
						<< ", tag " << i.spec_here().tag_lookup(i.tag_here()) 
						<< ", attributes: " << core::AttributeList(i.attributes_here(), i.get_handle())
						<< endl;
			return s;
		}
		std::ostream& operator<<(std::ostream& s, const AttributeList& attrs)
		{
			for (int i = 0; i < attrs.get_len(); ++i)
			{
				s << dwarf::spec::DEFAULT_DWARF_SPEC.attr_lookup(attrs[i].attr_here());
				try
				{
					encap::attribute_value v(attrs[i], attrs.d);
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
				// if we issued `it', we should have recorded its parent
				// FIXME: relax this policy perhaps, to allow soft cache?
				assert(found != parent_of.end());
				assert(found->first == it.offset_here());
				assert(found->second < it.offset_here());
				//cerr << "Parent cache says parent of 0x" << std::hex << found->first
				// << " is 0x" << std::hex << found->second << std::dec << endl;
				auto maybe_handle = Die::try_construct(*this, found->second);
				
				if (maybe_handle)
				{
					// update it
					iterator_base new_it(std::move(maybe_handle), it.get_depth() - 1, *this);
					assert(new_it.offset_here() == found->second);
					return new_it;
				}
				else
				{
					// no such DIE?!
					assert(false);
				}
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
			assert(&it.get_root() == this);
			Dwarf_Off start_offset = it.offset_here();
			Die::handle_type maybe_handle(nullptr, Die::deleter(nullptr)); // TODO: reenable deleter's default constructor
			if (start_offset == 0UL) 
			{
				// do the CU thing
				bool ret1 = clear_cu_context();
				assert(ret1);
				bool ret2 = advance_cu_context(); 
				if (!ret2) return iterator_base::END;
				maybe_handle = std::move(Die::try_construct(*this));
			}
			else
			{
				// do the non-CU thing
				maybe_handle = std::move(Die::try_construct(it));
			}
			// shared parent cache logic
			if (maybe_handle)
			{
				iterator_base new_it(std::move(maybe_handle), it.get_depth() + 1, it.get_root());
				// install in parent cache
				parent_of[new_it.offset_here()] = start_offset;
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
			assert(&it.get_root() == this);
			
			if (state == WITH_PAYLOAD) 
			{
				/* This means we *can* ask the payload. What will the 
				 * payload do by default? Call the root, of course. */
				auto p_with = dynamic_pointer_cast<with_named_children_die>(cur_payload);
				if (p_with)
				{
					return p_with->named_child(*p_root, name);
				}
			}
			return p_root->named_child(*this, name);
		}
		
		iterator_base
		root_die::find_named_child(const iterator_base& start, const string& name)
		{
			auto children = start.children_here();
			for (auto i_child = std::move(children.first); i_child != children.second; ++i_child)
			{
				if (i_child.name_here() == name) return i_child;
			}
			return iterator_base::END;
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
					 ||    current_cu_offset == *prev_next_cu_header + *first_cu_offset);
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
			auto found = parent_of.find(offset_here);
			// if we issued `it', we should have recorded its parent
			// FIXME: relax this policy perhaps, to allow soft cache?
			assert(found != parent_of.end());
			Dwarf_Off common_parent_offset = found->second;
			Die::handle_type maybe_handle(nullptr, Die::deleter(nullptr)); // TODO: reenable deleter default constructor
			
			if (it.tag_here() == DW_TAG_compile_unit)
			{
				// do the CU thing
				bool ret = set_cu_context(it.offset_here());
				assert(ret);
				ret = advance_cu_context();
				if (!ret) return iterator_base::END;
				maybe_handle = Die::try_construct(*this);
			}
			else
			{
				// do the non-CU thing
				maybe_handle = Die::try_construct(*this, it);
			}
			
			// shared parent cache logic
			if (maybe_handle)
			{
				auto new_it = iterator_base(std::move(maybe_handle), it.get_depth(), *this);
				// install in parent cache
				parent_of[new_it.offset_here()] = common_parent_offset;
				return new_it;
			} else return iterator_base::END;
		}
		
		bool 
		root_die::move_to_next_sibling(iterator_base& it)
		{
			Dwarf_Off start_off = it.offset_here();
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
				it.cur_payload = core::factory::for_spec(it.spec_here()).make_payload(std::move(it.get_handle().handle), *this);
				it.state = iterator_base::WITH_PAYLOAD;
				
				if (it.tag_here() != DW_TAG_compile_unit)
				{
					cerr << "Warning: made payload for non-CU " << endl;
				}
				
				return it.cur_payload;
			}
		}
		/* NOTE: I was thinking to put all factory code in namespace spec, 
		 * so that we can do 
		 * get_spec().factory(), 
		 * requiring all sub-namespace factories (lib::, encap::, core::)
		 * are centralised in spec
		 * (rather than trying to do core::factory<dwarf3_def::inst>.make_payload(handle), 
		 * which wouldn't let us do get_spec().factory()...
		 * BUT
		 * core::factory_for(dwarf3_def::inst).make_payload(handle) WOULD swork. So
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
			return get_handle().offset_here();
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
			return get_handle().tag_here();
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
		std::unique_ptr<const char, string_deleter>
		iterator_base::name_here() const
		{
			if (!is_real_die_position()) return nullptr;
			return get_handle().name_here();
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
			return get_handle().has_attr_here(attr);
		}		
		iterator_base iterator_base::nearest_enclosing(Dwarf_Half tag) const
		{
			if (tag == DW_TAG_compile_unit)
			{
				Dwarf_Off cu_off = enclosing_cu_offset_here();
				return p_root->pos(cu_off, 1);
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
                    case DW_OP_fbreg: {
                    	if (!frame_base) goto logic_error;
                        m_stack.push(*frame_base + i->lr_number);
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
						 * DWARF evaluator has just computed its *value*. We don't
						 * support this for now, and treat it just like missing regs. */
						throw No_entry();
					case DW_OP_deref_size:
					case DW_OP_deref:
						/* FIXME: we can do this one if we have p_mem analogous to p_regs. */
						throw No_entry();
					default:
						std::cerr << "Error: unrecognised opcode: " << spec.op_lookup(i->lr_atom) << std::endl;
						throw Not_supported("unrecognised opcode");
					no_regs:
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
		/* begin pasted from adt.cpp */
		boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned> 
		with_static_location_die::file_relative_intervals(
		
			root_die& r, 
		
			sym_binding_t (*sym_resolve)(const std::string& sym, void *arg), 
			void *arg /* = 0 */) const
		{
			auto nonconst_this = const_cast<with_static_location_die *>(this);
			auto attrs = nonconst_this->get_attrs();
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
					auto rangelist = enclosing_compile_unit()->normalize_rangelist(
						found_ranges->second.get_rangelist()
					);
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
				else if (found_low_pc != attrs.end() && found_high_pc != attrs.end())
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
							auto calculated_byte_size = dynamic_pointer_cast<spec::type_die>(
								get_ds()[found_type->second.get_ref().off]
							)->calculate_byte_size();
							assert(calculated_byte_size);
							opt_byte_size = *calculated_byte_size;
						}
					}
					assert(opt_byte_size);
					Dwarf_Unsigned byte_size = *opt_byte_size;
					if (byte_size == 0)
					{
						cerr << "Zero-length object: " << this->summary() << endl;
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
								this->get_spec()).tos();

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
		with_static_location_die::contains_addr(Dwarf_Addr file_relative_address,
			sym_binding_t (*sym_resolve)(const std::string& sym, void *arg), 
			void *arg /* = 0 */) const
		{
			// FIXME: cache intervals
			auto intervals = file_relative_intervals(sym_resolve, arg);
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
        	auto attrs = const_cast<with_static_location_die *>(this)->get_attrs();
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
/* from spec::subprogram_die */
		opt< std::pair<Dwarf_Off, std::shared_ptr<with_dynamic_location_die> > >
        subprogram_die::contains_addr_as_frame_local_or_argument( 
            	    Dwarf_Addr absolute_addr, 
                    Dwarf_Off dieset_relative_ip, 
                    Dwarf_Signed *out_frame_base,
                    dwarf::lib::regs *p_regs/* = 0*/) const
        {
        	/* auto nonconst_this = const_cast<subprogram_die *>(this); */
        	assert(this->get_frame_base());
			
			// Calculate the vaddr which selects a loclist element
			auto frame_base_loclist = *get_frame_base();
			auto enclosing_cu = enclosing_compile_unit();
			cerr << "Enclosing CU is " << enclosing_cu->summary() << endl;
			Dwarf_Addr low_pc = enclosing_cu->get_low_pc()->addr;
			assert(low_pc <= dieset_relative_ip);
			Dwarf_Addr vaddr = dieset_relative_ip - low_pc;
			/* Now calculate our frame base address. */
            auto frame_base_addr = dwarf::lib::evaluator(
                frame_base_loclist,
                vaddr,
                this->get_ds().get_spec(),
                p_regs).tos();
            if (out_frame_base) *out_frame_base = frame_base_addr;
            
            if (!this->first_child_offset()) return 0;
			/* Now we walk children
			 * (not just immediate children, because more might hide under lexical_blocks), 
			 * looking for with_dynamic_location_dies, and 
			 * call contains_addr on what we find.
			 * We skip contained DIEs that do not contain objects located in this frame. 
			 */
			struct stack_location_subtree_policy :  public spec::abstract_dieset::bfs_policy
			{
				typedef spec::abstract_dieset::bfs_policy super;
				int increment(abstract_dieset::iterator_base& base)
				{
					/* If our current DIE is 
					 * a with_dynamic_location_die
					 * OR
					 * is in the "interesting set"
					 * of DIEs that have no location but might contain such DIEs,
					 * we increment *with* enqueueing children.
					 * Otherwise we increment without enqueueing children.
					 */
					if (dynamic_pointer_cast<spec::with_dynamic_location_die>(base.p_d))
					{
						return super::increment(base);
					}
					else
					{
						switch (base.p_d->get_tag())
						{
							case DW_TAG_lexical_block:
								return super::increment(base);
								break;
							default:
								return super::increment_skipping_subtree(base);
								break;
						}
					}
				}
			} bfs_state;
			//abstract_dieset::bfs_policy bfs_state;
            abstract_dieset::iterator start_iterator
             = this->get_first_child()->iterator_here(bfs_state);
            std::cerr << "Exploring stack-located children of " << this->summary() << std::endl;
            unsigned initial_depth = start_iterator.base().path_from_root.size();
            for (auto i_bfs = start_iterator;
            		i_bfs.base().path_from_root.size() >= initial_depth;
                    ++i_bfs)
            {
            	std::cerr << "Considering whether DIE has stack location: " << (*i_bfs)->summary() << std::endl;
            	auto with_stack_loc = std::dynamic_pointer_cast<spec::with_dynamic_location_die>(
                	*i_bfs);
                if (!with_stack_loc) continue;
                
                opt<Dwarf_Off> result = with_stack_loc->contains_addr(absolute_addr,
                	frame_base_addr,
                    dieset_relative_ip,
                    p_regs);
                if (result) return std::make_pair(*result, with_stack_loc);
            }
            return opt< std::pair<Dwarf_Off, std::shared_ptr<with_dynamic_location_die> > >();
        }
        bool subprogram_die::is_variadic() const
        {
    	    try
            {
    	        for (auto i_child = this->get_first_child(); i_child;  // term'd by exception
            	    i_child = i_child->get_next_sibling())
                {
                    if (i_child->get_tag() == DW_TAG_unspecified_parameters)
                    {
            	        return true;
                    }
                }
        	}
            catch (No_entry) {}
            return false;
        }
/* from spec::with_dynamic_location_die */
		std::shared_ptr<spec::program_element_die> 
		with_dynamic_location_die::get_instantiating_definition() const
		{
			/* We want to return a parent DIE describing the thing whose instances
			 * contain instances of us. 
			 *
			 * If we're a member or inheritance, it's our nearest enclosing type.
			 * If we're a variable or fp, it's our enclosing subprogram.
			 * This might be null if we're actually a static variable. */
			// HACK: this should arguably be in overrides for formal_parameter and variable
			if (this->get_tag() == DW_TAG_formal_parameter
			||  this->get_tag() == DW_TAG_variable) 
			{
				return std::dynamic_pointer_cast<dwarf::spec::program_element_die>(
					nearest_enclosing(DW_TAG_subprogram));
			}
			else
			{
				std::shared_ptr<dwarf::spec::basic_die> candidate = this->get_parent();
				while (candidate 
					&& !std::dynamic_pointer_cast<dwarf::spec::type_die>(candidate))
				{
					candidate = candidate->get_parent();
				}
				return std::dynamic_pointer_cast<dwarf::spec::program_element_die>(candidate);
			}
		}

/* from spec::with_dynamic_location_die */
		opt<Dwarf_Off> with_dynamic_location_die::contains_addr_on_stack(
                    Dwarf_Addr absolute_addr,
                    Dwarf_Signed frame_base_addr,
                    Dwarf_Off dieset_relative_ip,
                    dwarf::lib::regs *p_regs) const
        {
        	auto attrs = const_cast<with_dynamic_location_die *>(this)->get_attrs();
			if (attrs.find(DW_AT_location) == attrs.end())
			{
				cerr << "Warning: " << this->summary() << " has no DW_AT_location; "
					<< "assuming it does not cover any stack locations." << endl;
				return opt<Dwarf_Off>();
			}
            auto base_addr = calculate_addr_on_stack(
				frame_base_addr,
				dieset_relative_ip,
				p_regs);
			std::cerr << "Calculated that an instance of DIE" << this->summary()
				<< " has base addr 0x" << std::hex << base_addr << std::dec;
            assert(attrs.find(DW_AT_type) != attrs.end());
            auto size = *(attrs.find(DW_AT_type)->second.get_refdie_is_type()->calculate_byte_size());
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
			
			// we need an enclosing subprogram or lexical_block
			auto p_lexical = this->nearest_enclosing(DW_TAG_lexical_block);
			auto p_subprogram = this->nearest_enclosing(DW_TAG_subprogram);
			if (!p_lexical && !p_subprogram) throw No_entry();
			
			return 	absolute_loclist_to_additive_loclist(
				*this->get_location());
		}
/* from spec::with_dynamic_location_die */
		opt<Dwarf_Off> with_dynamic_location_die::contains_addr_in_object(
                    Dwarf_Addr absolute_addr,
                    Dwarf_Signed object_base_addr,
                    Dwarf_Off dieset_relative_ip,
                    dwarf::lib::regs *p_regs) const
        {
        	auto attrs = const_cast<with_dynamic_location_die *>(this)->get_attrs();
            auto base_addr = calculate_addr_in_object(
				object_base_addr, dieset_relative_ip, p_regs);
            assert(attrs.find(DW_AT_type) != attrs.end());
            auto size = *(attrs.find(DW_AT_type)->second.get_refdie_is_type()->calculate_byte_size());
            if (absolute_addr >= base_addr
            &&  absolute_addr < base_addr + size)
            {
 				return absolute_addr - base_addr;
            }
            return opt<Dwarf_Off>();
        }
/* from with_dynamic_location_die, object-based cases */
		encap::loclist member_die::get_dynamic_location() const
		{
			/* These guys have loclists that adding to what on the
			   top-of-stack, which is what we want. */
			return *this->get_data_member_location();
		}
		encap::loclist inheritance_die::get_dynamic_location() const
		{
			return *this->get_data_member_location();
		}
/* from spec::with_dynamic_location_die */
		Dwarf_Addr 
		with_dynamic_location_die::calculate_addr_on_stack(
				Dwarf_Addr frame_base_addr,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs/* = 0*/) const
		{
        	auto attrs = const_cast<with_dynamic_location_die *>(this)->get_attrs();
            assert(attrs.find(DW_AT_location) != attrs.end());
			Dwarf_Addr dieset_relative_cu_base_ip = (this->enclosing_compile_unit()->get_low_pc() ? 
				    this->enclosing_compile_unit()->get_low_pc()->addr : 0 );
			if (dieset_relative_ip < dieset_relative_cu_base_ip)
			{
				cerr << "Warning: bad relative IP (0x" << std::hex << dieset_relative_ip << std::dec
					<< ") for stack location of DIE in compile unit "
					<< *this->enclosing_compile_unit()
					<< ": " << *this << endl;
				throw No_entry();
			}
			return (Dwarf_Addr) dwarf::lib::evaluator(
				attrs.find(DW_AT_location)->second.get_loclist(),
				dieset_relative_ip // needs to be CU-relative
				 - dieset_relative_cu_base_ip,
				this->get_ds().get_spec(),
				p_regs,
				frame_base_addr).tos();
		}
		Dwarf_Addr
		with_dynamic_location_die::calculate_addr_in_object(
				Dwarf_Addr object_base_addr,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs /*= 0*/) const
		{
        	auto attrs = const_cast<with_dynamic_location_die *>(this)->get_attrs();
            assert(attrs.find(DW_AT_data_member_location) != attrs.end());
			return (Dwarf_Addr) dwarf::lib::evaluator(
				attrs.find(DW_AT_data_member_location)->second.get_loclist(),
				dieset_relative_ip == 0 ? 0 : // if we specify it, needs to be CU-relative
				 - (this->enclosing_compile_unit()->get_low_pc() ? 
				 	this->enclosing_compile_unit()->get_low_pc()->addr : (Dwarf_Addr)0),
				this->get_ds().get_spec(),
				p_regs,
				object_base_addr, // ignored
				std::stack<Dwarf_Unsigned>(std::deque<Dwarf_Unsigned>(1, object_base_addr))).tos();
		}
/* from spec::with_named_children_die */
        std::shared_ptr<spec::basic_die>
        with_named_children_die::named_child(const std::string& name) 
        { 
			try
            {
            	for (auto current = this->get_first_child();
                		; // terminates by exception
                        current = current->get_next_sibling())
                {
                	if (current->get_name() 
                    	&& *current->get_name() == name) return current;
                }
            }
            catch (No_entry) { return shared_ptr<spec::basic_die>(); }
        }

        std::shared_ptr<spec::basic_die> 
        with_named_children_die::resolve(const std::string& name) 
        {
            std::vector<std::string> multipart_name;
            multipart_name.push_back(name);
            return resolve(multipart_name.begin(), multipart_name.end());
        }

        std::shared_ptr<spec::basic_die> 
        with_named_children_die::scoped_resolve(const std::string& name) 
        {
            std::vector<std::string> multipart_name;
            multipart_name.push_back(name);
            return scoped_resolve(multipart_name.begin(), multipart_name.end());
        }
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
			switch(this->get_language())
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
		shared_ptr<type_die> compile_unit_die::implicit_enum_base_type() const
		{
			auto nonconst_this = const_cast<compile_unit_die *>(this);
			switch(this->get_language())
			{
				case DW_LANG_C:
				case DW_LANG_C89:
				case DW_LANG_C_plus_plus:
				case DW_LANG_C99: {
					const char *attempts[] = { "signed int", "int" };
					auto total_attempts = sizeof attempts / sizeof attempts[0];
					for (auto i_attempt = 0; i_attempt < total_attempts; ++i_attempt)
					{
						auto found = nonconst_this->named_child(attempts[i_attempt]);
						if (found) return dynamic_pointer_cast<type_die>(found);
					}
					assert(false && "enum but no int or signed int");
				}
				default:
					return shared_ptr<type_die>();
			}
		}
	}
} // end namespace dwarf
