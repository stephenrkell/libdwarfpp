/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * dwarflib-handles.cpp: short libdwarf/libdw-specific handle methods
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#include "dwarfpp/abstract.hpp"
#include "dwarfpp/abstract-inl.hpp"
#include "dwarfpp/dwarflib-handles.hpp"
#include "dwarfpp/root.hpp"
#include "dwarfpp/root-inl.hpp"
#include "dwarfpp/iter.hpp"
#include "dwarfpp/iter-inl.hpp"
#include "dwarfpp/dies.hpp"
#include "dwarfpp/dies-inl.hpp"

namespace dwarf
{
	namespace core
	{
		Die::handle_type 
		Die::try_construct(root_die& r, const iterator_base& it) /* siblingof */
		{
			raw_handle_type returned;
			if (!dynamic_cast<Die *>(&it.get_handle())) return handle_type(nullptr, deleter(nullptr, r));
			int ret = dwarf_siblingof(r.dbg.handle.get(), dynamic_cast<Die&>(it.get_handle()).handle.get(), 
			    &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned, deleter(r.dbg.handle.get(), r));
			else return handle_type(nullptr, deleter(nullptr, r));
		}
		Die::handle_type 
		Die::try_construct(root_die& r) /* siblingof in "first DIE of current CU" case */
		{
			raw_handle_type returned;
			if (!r.dbg.handle) return handle_type(nullptr, deleter(nullptr, r));
			int ret = dwarf_siblingof(r.dbg.handle.get(), nullptr, &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned, deleter(r.dbg.handle.get(), r));
			else return handle_type(nullptr, deleter(nullptr, r));
		}
		Die::handle_type 
		Die::try_construct(const iterator_base& it) /* child */
		{
			raw_handle_type returned;
			root_die& r = it.get_root();
			if (!dynamic_cast<Die *>(&it.get_handle())) return handle_type(nullptr, deleter(nullptr, r));
			int ret = dwarf_child(dynamic_cast<Die&>(it.get_handle()).handle.get(), &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned, deleter(it.get_root().dbg.handle.get(), r));
			else return handle_type(nullptr, deleter(nullptr, r));
		}
		Die::handle_type 
		Die::try_construct(root_die& r, Dwarf_Off off) /* offdie */
		{
			raw_handle_type returned;
			if (!r.dbg.handle) return handle_type(nullptr, deleter(nullptr, r));
			int ret = dwarf_offdie(r.dbg.handle.get(), off, &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned, deleter(r.dbg.handle.get(), r));
			else return handle_type(nullptr, deleter(nullptr, r));
		}
		
		/* FIXME: just build iterators directly?
		 * Or at least move the cache logic out of here and into the 
		 * iterator constructors. Then also eliminate the
		 * try_construct stuff and just do it in the Die constructors.
		 * We use try_construct in root_die::pos, but we should use a
		 * constructor on iterator_base.
		 * We use try_construct in iterator_base's copy constructor,
		 * but we should just define a copy constructor for Die.
		 * Also see how much we use attrs_here() / attributes_here(). */

		Die::Die(root_die& r, const iterator_base& it) /* siblingof */
		 : handle(try_construct(r, it))
		{ 
			if (!this->handle) throw Error(current_dwarf_error, 0);
			// also update the parent cache and sibling cache.
			// 1. "it"'s parent is our parent; what is "it"'s parent?
			Dwarf_Off off = this->offset_here();
			opt<Dwarf_Off> it_parent_off;
			if (it.fast_deref() && it.fast_deref()->cached_parent_off) it_parent_off = it.fast_deref()->cached_parent_off;
			else
			{
				auto found = r.parent_of.find(it.offset_here());
				if (found != r.parent_of.end()) it_parent_off = opt<Dwarf_Off>(found->second);
			}
			if (it_parent_off)
			{
				// parent of the sibling is the same as parent of "it"
				r.parent_of[off] = *it_parent_off;
				// no payload yet; FIXME we should really store this in the iterator
				// so that when we create payload we can populate its cache right away
			} else debug() << "Warning: parent cache did not know 0x" << std::hex << it.offset_here() << std::dec << std::endl;

			// 2. we are the next sibling of "it"
			r.next_sibling_of[it.offset_here()] = off;
			if (it.fast_deref()) it.fast_deref()->cached_next_sibling_off = opt<Dwarf_Off>(off);
		}
		Die::Die(root_die& r) /* siblingof in "first die of CU" case */
		 : handle(try_construct(r))
		{ 
			if (!this->handle) throw Error(current_dwarf_error, 0); 
			// update parent cache
			Dwarf_Off off = this->offset_here();
			r.parent_of[off] = 0UL; // FIXME: looks wrong
			// the *caller* updates first_child_of, next_sibling_of
		} 
		Die::Die(root_die& r, Dwarf_Off off) /* offdie */
		 : handle(try_construct(r, off))
		{ 
			if (!this->handle) throw Error(current_dwarf_error, 0);
			// NOTE: we don't know our parent -- can't update parent cache
		} 
		Die::Die(const iterator_base& it) /* child */
		 : handle(try_construct(it))
		{
			root_die& r = it.get_root();
			if (!this->handle) throw Error(current_dwarf_error, 0);
			Dwarf_Off off = this->offset_here();
			r.parent_of[off] = it.offset_here();
			// first_child_of, next_sibling_of
			r.first_child_of[it.offset_here()] = off;
		}
		
		spec& Die::spec_here() const
		{
			// HACK: avoid creating any payload for now, for speed-testing
			return ::dwarf::spec::dwarf_current;
			
			/* NOTE: subtlety concerning payload construction. To make the 
			 * payload, we need the factory, meaning we need the spec. 
			 * BUT to read the spec, we need the CU payload! 
			 * Escape this by ensuring that root_die::make_payload, in the CU case,
			 * uses the default factory. In turn, we ensure that all factories, 
			 * for the CU case, behave the same. I have refactored
			 * the factory base class so that only non-CU DIEs get the make_payload
			 * call.  */

			// this spec_here() is only used from the factory, and not 
			// used in the CU case because the factory handles that specially
			assert(tag_here() != DW_TAG_compile_unit); 
			
			Dwarf_Off cu_offset = enclosing_cu_offset_here();
			// this should be sticky, hence fast to find
			return get_constructing_root().pos(cu_offset, 1, opt<Dwarf_Off>(0UL)).spec_here();

// 			{
// 				return 
// 				if (state == HANDLE_ONLY)
// 				{
// 					p_root->make_payload(*this);
// 				}
// 				assert(cur_payload && state == WITH_PAYLOAD);
// 				auto p_cu = dynamic_pointer_cast<compile_unit_die>(cur_payload);
// 				assert(p_cu);
// 				switch(p_cu->version_stamp)
// 				{
// 					case 2: return ::dwarf::spec::dwarf_current;
// 					default: assert(false);
// 				}
// 			}
// 			else
// 			{
// 				// NO! CUs now lie about their spec
// 				// return nearest_enclosing(DW_TAG_compile_unit).spec_here();
		
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
		
		std::ostream& operator<<(std::ostream& s, const AttributeList& attrs)
		{
			for (int i = 0; i < attrs.get_len(); ++i)
			{
				s << dwarf::spec::DEFAULT_DWARF_SPEC.attr_lookup(attrs[i].attr_here());
				encap::attribute_value v(attrs[i], attrs.d, attrs.d.get_constructing_root());
				s << ": " << v << std::endl;
			}
			return s;
		}
		Dwarf_Off Die::offset_here() const
		{
			Dwarf_Off off;
			int ret = dwarf_dieoffset(raw_handle(), &off, &current_dwarf_error);
			assert(ret == DW_DLV_OK);
			return off;
		}
		Dwarf_Half Die::tag_here() const
		{
			Dwarf_Half tag;
			int ret = dwarf_tag(handle.get(), &tag, &current_dwarf_error);
			assert(ret == DW_DLV_OK);
			return tag;
		}
		std::unique_ptr<const char, string_deleter>
		Die::name_here() const
		{
			char *str;
			int ret = dwarf_diename(raw_handle(), &str, &current_dwarf_error);
			if (ret == DW_DLV_NO_ENTRY) return nullptr;
			if (ret == DW_DLV_OK) return unique_ptr<const char, string_deleter>(
				str, string_deleter(get_dbg()));
			//debug() << "Aborting with " << ret << " from dwarf_diename ("
			//	<< dwarf_errormsg(current_dwarf_error) << ")" << std::endl; 
			abort();
		}
		bool Die::has_attr_here(Dwarf_Half attr) const
		{
			Dwarf_Bool returned;
			int ret = dwarf_hasattr(raw_handle(), attr, &returned, &current_dwarf_error);
			assert(ret == DW_DLV_OK);
			return returned;
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
	
	}
}
