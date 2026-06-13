/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 *
 * libdw-handles.cpp: handle methods for the direct elfutils' libdw backend.
 *
 * This is the libdw counterpart of libdwarf-handles.cpp
 *
 * Copyright (c) 2008--26, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */
// remember the goal here: to allow easy rebasing of the library
// (with a bit of #ifdef'ing)
// ... onto DWARF libraries that aren't libdwarf.
// ... consider libdw1, LLVM stuff, etc..

#include "dwarfpp/abstract.hpp"
#include "dwarfpp/abstract-inl.hpp"
#include "dwarfpp/handles.hpp"
#include "dwarfpp/root.hpp"
#include "dwarfpp/root-inl.hpp"
#include "dwarfpp/iter.hpp"
#include "dwarfpp/iter-inl.hpp"
#include "dwarfpp/dies.hpp"
#include "dwarfpp/dies-inl.hpp"

/* ====================================================================== *
 *  Direct libdw backend: handle methods over the allocation-free helpers  *
 *  in src/libdw.cpp. A Die / Attribute holds libdw's by-value struct       *
 *  inline (handle.d / handle.a), so construction just fills that storage   *
 *  in place -- no heap allocation, no dwarf_dealloc, per node.             *
 * ====================================================================== */
namespace dwarf
{
	namespace core
	{
		Die::handle_type
		Die::try_construct(root_die& r, const iterator_base& it) /* siblingof */
		{
			if (!it.get_handle_die()) return handle_type(nullptr, deleter(nullptr, r));
			Dwarf_Die_s out;
			int ret = dwarfpp_siblingof(r.dbg.raw_handle(),
				(*it.get_handle_die()).raw_handle(), &out);
			if (ret == DW_DLV_OK) return handle_type(out, deleter(r.dbg.raw_handle(), r));
			else return handle_type(nullptr, deleter(nullptr, r));
		}
		Die::handle_type
		Die::try_construct(root_die& r) /* "first DIE of current CU" case */
		{
			if (!r.dbg.handle) return handle_type(nullptr, deleter(nullptr, r));
			Dwarf_Die_s out;
			int ret = dwarfpp_cu_die(r.dbg.raw_handle(), &out);
			if (ret == DW_DLV_OK) return handle_type(out, deleter(r.dbg.raw_handle(), r));
			else return handle_type(nullptr, deleter(nullptr, r));
		}
		Die::handle_type
		Die::try_construct(const iterator_base& it) /* child */
		{
			root_die& r = it.get_root();
			if (!it.get_handle_die()) return handle_type(nullptr, deleter(nullptr, r));
			Dwarf_Die_s out;
			int ret = dwarfpp_child((*it.get_handle_die()).raw_handle(), &out);
			if (ret == DW_DLV_OK) return handle_type(out, deleter(r.dbg.raw_handle(), r));
			else return handle_type(nullptr, deleter(nullptr, r));
		}
		Die::handle_type
		Die::try_construct(root_die& r, Dwarf_Off off) /* offdie */
		{
			if (!r.dbg.handle) return handle_type(nullptr, deleter(nullptr, r));
			Dwarf_Die_s out;
			int ret = dwarfpp_offdie(r.dbg.raw_handle(), off, &out);
			if (ret == DW_DLV_OK) return handle_type(out, deleter(r.dbg.raw_handle(), r));
			else return handle_type(nullptr, deleter(nullptr, r));
		}

		/* The throwing constructors and their parent/sibling/child cache
		 * bookkeeping are backend-neutral -- identical to the libdwarf layer. */
		Die::Die(root_die& r, const iterator_base& it) /* siblingof */
		 : handle(try_construct(r, it))
		{
			if (!this->handle) throw Error(current_dwarf_error, 0);
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
				r.parent_of[off] = *it_parent_off;
			} else debug() << "Warning: parent cache did not know 0x" << std::hex << it.offset_here() << std::dec << std::endl;

			r.next_sibling_of[it.offset_here()] = off;
			if (it.fast_deref()) it.fast_deref()->cached_next_sibling_off = opt<Dwarf_Off>(off);
		}
		Die::Die(root_die& r) /* siblingof in "first die of CU" case */
		 : handle(try_construct(r))
		{
			if (!this->handle) throw Error(current_dwarf_error, 0);
			Dwarf_Off off = this->offset_here();
			r.parent_of[off] = 0UL;
		}
		Die::Die(root_die& r, Dwarf_Off off) /* offdie */
		 : handle(try_construct(r, off))
		{
			if (!this->handle) throw Error(current_dwarf_error, 0);
		}
		Die::Die(const iterator_base& it) /* child */
		 : handle(try_construct(it))
		{
			root_die& r = it.get_root();
			if (!this->handle) throw Error(current_dwarf_error, 0);
			Dwarf_Off off = this->offset_here();
			r.parent_of[off] = it.offset_here();
			r.first_child_of[it.offset_here()] = off;
		}

		spec& Die::spec_here() const
		{
			// HACK: avoid creating any payload for now, for speed-testing
			return ::dwarf::spec::dwarf_current;
		}

		Debug::Debug(int fd)
		{
			Dwarf_Debug returned;
			int ret = dwarf_init(fd, DW_DLC_READ, exception_error_handler,
				nullptr, &returned, &current_dwarf_error);
			if (ret != DW_DLV_OK) throw No_entry();
			this->handle = handle_type(returned);
		}
		Debug::Debug(Elf *elf)
		{
			Dwarf_Debug returned;
			int ret = dwarf_elf_init(reinterpret_cast<dwarf::lib::Elf_opaque_in_libdwarf*>(elf),
				DW_DLC_READ, exception_error_handler,
				nullptr, &returned, &current_dwarf_error);
			if (ret != DW_DLV_OK) throw No_entry();
			this->handle = handle_type(returned);
		}
		void
		Debug::deleter::operator()(raw_handle_type arg) const
		{
			dwarf_finish(arg, &current_dwarf_error);
		}

		/* ---- AttributeList: one dwarf_getattrs pass into one vector ------ */
		namespace {
			struct attr_collect_ctx { vector<Attribute> *out; Dwarf_Debug dbg; };
			int attr_collect_cb(Dwarf_Attribute a, void *arg)
			{
				attr_collect_ctx *ctx = static_cast<attr_collect_ctx*>(arg);
				ctx->out->push_back(Attribute(Attribute::handle_type(*a,
					Attribute::deleter(ctx->dbg))));
				return 0; /* DWARF_CB_OK -> keep going */
			}
		}
		AttributeList::AttributeList(const Die& die)
		 : d(die)
		{
			attr_collect_ctx ctx { &copied_list, die.get_dbg() };
			dwarfpp_getattrs(die.raw_handle(), attr_collect_cb, &ctx);
		}
		AttributeList::handle_type
		AttributeList::try_construct(const Die& die)
		{
			handle_type v(new vector<Attribute>());
			attr_collect_ctx ctx { v.get(), die.get_dbg() };
			dwarfpp_getattrs(die.raw_handle(), attr_collect_cb, &ctx);
			return v;
		}
		AttributeList::AttributeList(handle_type h, const Die& d)
		 : d(d)
		{
			if (h) copied_list = std::move(*h);
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

		/* ---- leaf accessors (call the thin wrappers in libdw.cpp) -------- */
		Dwarf_Off Die::offset_here() const
		{
			Dwarf_Off off;
			int ret = dwarf_dieoffset(raw_handle(), &off, nullptr);
			assert(ret == DW_DLV_OK);
			return off;
		}
		Dwarf_Half Die::tag_here() const
		{
			Dwarf_Half tag;
			int ret = dwarf_tag(raw_handle(), &tag, nullptr);
			assert(ret == DW_DLV_OK);
			return tag;
		}
		std::unique_ptr<const char, string_deleter>
		Die::name_here() const
		{
			char *str;
			int ret = dwarf_diename(raw_handle(), &str, nullptr);
			if (ret == DW_DLV_NO_ENTRY) return nullptr;
			if (ret == DW_DLV_OK) return unique_ptr<const char, string_deleter>(
				str, string_deleter(get_dbg()));
			abort();
		}
		bool Die::has_attr_here(Dwarf_Half attr) const
		{
			Dwarf_Bool returned;
			int ret = dwarf_hasattr(raw_handle(), attr, &returned, nullptr);
			assert(ret == DW_DLV_OK);
			return returned;
		}
		Dwarf_Off Die::enclosing_cu_offset_here() const
		{
			Dwarf_Off cu_offset;
			int ret = dwarf_CU_dieoffset_given_die(raw_handle(),
				&cu_offset, nullptr);
			if (ret == DW_DLV_OK) return cu_offset;
			else assert(false);
		}

		Dwarf_Half Attribute::attr_here() const
		{
			Dwarf_Half attr;
			int ret = dwarf_whatattr(handle.get(), &attr, nullptr);
			assert(ret == DW_DLV_OK);
			return attr;
		}
		Dwarf_Half Attribute::form_here() const
		{
			Dwarf_Half form;
			int ret = dwarf_whatform(handle.get(), &form, nullptr);
			assert(ret == DW_DLV_OK);
			return form;
		}
	}
}
