/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 *
 * libdw-handles.hpp: C++ handle layer backed *directly* by elfutils' libdw.
 *
 * This is the libdw-backend counterpart of libdwarf-handles.hpp. 
 * The core difference is that it represents a DIE or an attribute as libdw's
 * small by-value struct held *inline* in the handle object -- not as an opaque,
 * heap-allocated, dwarf_dealloc'd pointer. 
 * 
 * Walking the DIE tree through this doesn't need to perform a per-node allocation.
 *
 * The colder handles avoid allocation too. Block holds libdw's block by value
 * inline (its bytes belong to libdw); RangesList and StringList own their
 * results in a single std::vector; and the location lists (Locdesc /
 * LocdescList) own their synthesised Dwarf_Loc descriptors inline -- each
 * handle keeps the Dwarf_Loc array in a vector and an inline Dwarf_Locdesc
 * whose ld_s points at it. So no cold path mallocs a libdwarf-shaped buffer or
 * relies on dwarf_dealloc any more, and the unused srcline/aranges/globals list
 * handles have been dropped.
 *
 * Copyright (c) 2008--26, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_LIBDW_HANDLES_HPP_
#define DWARFPP_LIBDW_HANDLES_HPP_

#include "config.h" /* our configure-generated header, for DWARFPP_USE_LIBDW */

#if not defined(DWARFPP_USE_LIBDW) || !DWARFPP_USE_LIBDW
#error "include/dwarfpp/libdw-handles.hpp is for the libdw backend only"
#endif

#include "dwarfpp/dwarfbackend.hpp"
#include "dwarfpp/abstract.hpp"

#include <iostream>
#include <utility>
#include <functional>
#include <vector>

namespace dwarf
{
	namespace core
	{
		using std::unique_ptr;
		using std::vector;
		using std::pair;
		using namespace dwarf::lib;
		/* Forward-declare what we assume from libdwarfpp. */
		struct root_die;
		struct iterator_base;
		struct abstract_die;

		void exception_error_handler(Dwarf_Error error, Dwarf_Ptr errarg);

		/* ---- Debug ------------------------------------------------------ *
		 * One ::Dwarf per file: a single allocation at open time, freed by
		 * dwarf_finish(). This handle is identical in shape to the libdwarf
		 * one -- the cost it carries is not on any hot path. */
		typedef struct Dwarf_Debug_s*      Dwarf_Debug;
		struct Debug
		{
			typedef Dwarf_Debug raw_handle_type;
			typedef Dwarf_Debug_s opaque_type;
			struct deleter
			{
				void operator ()(raw_handle_type arg) const;
			};
			typedef unique_ptr<opaque_type, deleter> handle_type;

			handle_type handle;

			Debug(int fd);
			Debug(Elf *elf);
			Debug() : handle(nullptr) {}

			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }
		};

		/* libdw owns the storage behind DIE names and form strings.
		 * We keep the string_deleter type for compatibility with the libdwarf layer
		 */
		struct string_deleter
		{
			Debug::raw_handle_type dbg;
			string_deleter(Debug::raw_handle_type dbg) : dbg(dbg) {}
			string_deleter() : dbg(nullptr) {}

			void operator()(const char *arg)
			{
				if (dbg)
				{
					dwarf_dealloc(dbg,
						const_cast<void*>(static_cast<const void *>(arg)),
						DW_DLA_STRING);
				} else assert(!arg);
			}
		};

		/* ---- Die -------------------------------------------------------- *
		 * Holds a libdw ::Dwarf_Die (mirrored as lib::Dwarf_Die_s) BY VALUE,
		 * inline. The `handle` member mimics the surface of the libdwarf
		 * handle's unique_ptr (bool test, get(), get_deleter(), move-only
		 * semantics that null the source) so that the consuming code in iter.hpp
		 * / root.cpp -- which manipulates Die::handle_type and Die::deleter
		 * directly -- works unchanged. But because a libdw die owns nothing,
		 * there is no allocation and no dwarf_dealloc: copying/destroying a
		 * handle is just copying/discarding 32 bytes of POD. */
		typedef struct Dwarf_Die_s*        Dwarf_Die;
		struct Die : /*private*/ virtual abstract_die
		{
			typedef Dwarf_Die raw_handle_type;
			typedef Dwarf_Die_s opaque_type;
			struct deleter
			{
				Debug::raw_handle_type dbg;
				root_die *p_constructing_root;
				deleter(Debug::raw_handle_type dbg, root_die& r)
				 : dbg(dbg), p_constructing_root(&r) {}
				deleter(Debug::raw_handle_type dbg)
				 : dbg(dbg), p_constructing_root(nullptr) {}
				/* No ownership to release: provided only for interface parity. */
				void operator ()(raw_handle_type) const {}
			};

			/* The inline-storage stand-in for unique_ptr<opaque_type, deleter>. */
			struct handle_type
			{
				Dwarf_Die_s d;      /* the libdw die value, valid iff `present` */
				bool present;
				deleter dlt;

				handle_type() : d{}, present(false), dlt(nullptr) {}
				handle_type(std::nullptr_t, const deleter& dl)
				 : d{}, present(false), dlt(dl) {}
				handle_type(const Dwarf_Die_s& dd, const deleter& dl)
				 : d(dd), present(true), dlt(dl) {}
				handle_type(handle_type&& o) noexcept
				 : d(o.d), present(o.present), dlt(o.dlt) { o.present = false; }
				handle_type& operator=(handle_type&& o) noexcept
				{ d = o.d; present = o.present; dlt = o.dlt; o.present = false; return *this; }
				handle_type(const handle_type&) = delete;
				handle_type& operator=(const handle_type&) = delete;

				explicit operator bool() const { return present; }
				raw_handle_type get() const
				{ return present ? const_cast<Dwarf_Die_s*>(&d) : nullptr; }
				deleter& get_deleter() { return dlt; }
				const deleter& get_deleter() const { return dlt; }
			};
			handle_type handle;
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; }
			root_die& get_constructing_root() const
			{ return *handle.get_deleter().p_constructing_root; }

			static handle_type
			try_construct(root_die& r, const iterator_base& die); /* siblingof */
			static handle_type
			try_construct(root_die& r); /* siblingof with null die */
			static handle_type
			try_construct(const iterator_base& die); /* child */
			static handle_type
			try_construct(root_die& r, Dwarf_Off off); /* offdie */

			// "upgrade" constructor
			Die(handle_type h) : handle(std::move(h)) {}

			// "nullptr" constructor
			Die(std::nullptr_t, root_die *p_r) : handle(nullptr, deleter(nullptr))
			{ handle.get_deleter().p_constructing_root = p_r; }

			// throwing constructors
			Die(root_die& r, const iterator_base& die); /* siblingof */
			explicit Die(root_die& r); /* siblingof in the root case */
			explicit Die(const iterator_base& die); /* child */
			Die(root_die& r, Dwarf_Off off); /* offdie */

			// move constructor / assignment
			Die(Die&& d) : handle(std::move(d.handle)) {}
			Die& operator=(Die&& d) { handle = std::move(d.handle); return *this; }

			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }

			// libdw-backed methods
			Dwarf_Off offset_here() const;
			Dwarf_Half tag_here() const;
			std::unique_ptr<const char, string_deleter> name_here() const;
			Dwarf_Off enclosing_cu_offset_here() const;
			bool has_attr_here(Dwarf_Half attr) const;
			bool has_attribute_here(Dwarf_Half attr) const { return has_attr_here(attr); }
			spec& spec_here() const;

			inline encap::attribute_map copy_attrs() const;

			friend class iterator_base;
			/* implement the abstract_die interface */
			inline Dwarf_Off get_offset() const { return offset_here(); }
			inline Dwarf_Half get_tag() const { return tag_here(); }
			inline opt<string> get_name() const
			{ return name_here() ? opt<string>(string(name_here().get())) : opt<string>(); }
			inline unique_ptr<const char, string_deleter> get_raw_name() const
			{ return name_here(); }
			inline Dwarf_Off get_enclosing_cu_offset() const
			{ return enclosing_cu_offset_here(); }
			inline bool has_attr(Dwarf_Half attr) const { return has_attr_here(attr); }
			inline spec& get_spec(root_die& r) const { return spec_here(); }
		};

		/* ---- Attribute -------------------------------------------------- *
		 * Holds a libdw ::Dwarf_Attribute (mirrored as lib::Dwarf_Attribute_s)
		 * BY VALUE, inline. As with Die, no allocation and no dwarf_dealloc. */
		typedef struct Dwarf_Attribute_s*  Dwarf_Attribute;
		struct Attribute
		{
			typedef Dwarf_Attribute raw_handle_type;
			typedef Dwarf_Attribute_s opaque_type;
			struct deleter
			{
				Debug::raw_handle_type dbg;
				deleter(Debug::raw_handle_type dbg) : dbg(dbg) {}
				void operator()(raw_handle_type) const {}
			};
			struct handle_type
			{
				Dwarf_Attribute_s a;
				bool present;
				deleter dlt;

				handle_type() : a{}, present(false), dlt(nullptr) {}
				handle_type(std::nullptr_t, const deleter& dl)
				 : a{}, present(false), dlt(dl) {}
				handle_type(const Dwarf_Attribute_s& aa, const deleter& dl)
				 : a(aa), present(true), dlt(dl) {}
				handle_type(handle_type&& o) noexcept
				 : a(o.a), present(o.present), dlt(o.dlt) { o.present = false; }
				handle_type& operator=(handle_type&& o) noexcept
				{ a = o.a; present = o.present; dlt = o.dlt; o.present = false; return *this; }
				handle_type(const handle_type&) = delete;
				handle_type& operator=(const handle_type&) = delete;

				explicit operator bool() const { return present; }
				raw_handle_type get() const
				{ return present ? const_cast<Dwarf_Attribute_s*>(&a) : nullptr; }
				deleter& get_deleter() { return dlt; }
				const deleter& get_deleter() const { return dlt; }
			};

			handle_type handle;
			Dwarf_Debug get_dbg() const { return handle.get_deleter().dbg; }

			static inline handle_type
			try_construct(const Die& it, Dwarf_Half attr);
			inline explicit Attribute(const Die& it, Dwarf_Half attr);
			inline Attribute(handle_type h) : handle(std::move(h)) {}
			Attribute(Attribute&&) = default;
			Attribute& operator=(Attribute&&) = default;

			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }

			Dwarf_Half attr_here() const;
			Dwarf_Half form_here() const;
		};

		/* ---- Locdesc ---------------------------------------------------- *
		 * A location expression decodes to a run of Dwarf_Loc operations. libdw
		 * hands these back as parsed Dwarf_Op[] (or, for a raw byte expression,
		 * we decode them ourselves), so -- unlike a DIE or Attribute -- the
		 * libdwarf-shaped descriptor has to be materialised. We materialise it
		 * inline: the handle owns the Dwarf_Loc array in a vector and an inline
		 * Dwarf_Locdesc whose ld_s points at that vector's storage. No malloc, no
		 * dwarf_dealloc; the handle mimics enough of unique_ptr (bool, get())
		 * for the consuming code. ld_s is re-pointed on every move, since the
		 * vector's buffer is what the descriptor refers to. */
		struct Locdesc
		{
			typedef Dwarf_Locdesc *raw_handle_type;
			struct deleter
			{
				Debug::raw_handle_type dbg;
				deleter(Debug::raw_handle_type dbg) : dbg(dbg) {}
				/* The vector owns ld_s; nothing to release here. */
				void operator()(raw_handle_type) const {}
			};
			struct handle_type
			{
				vector<Dwarf_Loc> ops;  /* backing storage for desc.ld_s */
				Dwarf_Locdesc desc;     /* ld_s/ld_cents re-pointed at ops */
				bool present;
				deleter dlt;

				void repoint()
				{ desc.ld_s = ops.data(); desc.ld_cents = (Dwarf_Half) ops.size(); }

				handle_type() : ops(), desc{}, present(false), dlt(nullptr) {}
				handle_type(std::nullptr_t, const deleter& dl)
				 : ops(), desc{}, present(false), dlt(dl) {}
				handle_type(vector<Dwarf_Loc>&& o, const Dwarf_Locdesc& d, const deleter& dl)
				 : ops(std::move(o)), desc(d), present(true), dlt(dl) { repoint(); }
				handle_type(handle_type&& o) noexcept
				 : ops(std::move(o.ops)), desc(o.desc), present(o.present), dlt(o.dlt)
				{ repoint(); o.present = false; }
				handle_type& operator=(handle_type&& o) noexcept
				{
					ops = std::move(o.ops); desc = o.desc; present = o.present;
					dlt = o.dlt; repoint(); o.present = false; return *this;
				}
				handle_type(const handle_type&) = delete;
				handle_type& operator=(const handle_type&) = delete;

				explicit operator bool() const { return present; }
				raw_handle_type get() const
				{ return present ? const_cast<Dwarf_Locdesc*>(&desc) : nullptr; }
				deleter& get_deleter() { return dlt; }
				const deleter& get_deleter() const { return dlt; }
			};

			handle_type handle;
			Dwarf_Debug get_dbg() const { return handle.get_deleter().dbg; }

			inline Locdesc(handle_type h) : handle(std::move(h)) {}
			static inline handle_type
			try_construct(const Attribute& a);
			static inline handle_type
			try_construct(Dwarf_Debug dbg, Dwarf_Ptr bytes_in, Dwarf_Unsigned bytes_len);

			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }
		};

		/* Block is special because it doesn't have an opaque type: the Dwarf_Block
		 * *is* the value. Like Die/Attribute, we hold it BY VALUE, inline -- its
		 * bl_data points into the section storage that the ::Dwarf owns, so there
		 * is no allocation and nothing to dwarf_dealloc. The handle mimics enough
		 * of unique_ptr (bool, get(), operator->, get_deleter()) for the consuming
		 * code. */
		struct Block
		{
			typedef Dwarf_Block *raw_handle_type;
			struct deleter
			{
				Debug::raw_handle_type dbg;
				deleter(Debug::raw_handle_type dbg) : dbg(dbg) {}
				/* No ownership to release: provided only for interface parity. */
				void operator()(raw_handle_type) const {}
			};
			struct handle_type
			{
				Dwarf_Block b;
				bool present;
				deleter dlt;

				handle_type() : b{}, present(false), dlt(nullptr) {}
				handle_type(std::nullptr_t, const deleter& dl)
				 : b{}, present(false), dlt(dl) {}
				handle_type(const Dwarf_Block& bb, const deleter& dl)
				 : b(bb), present(true), dlt(dl) {}
				handle_type(handle_type&& o) noexcept
				 : b(o.b), present(o.present), dlt(o.dlt) { o.present = false; }
				handle_type& operator=(handle_type&& o) noexcept
				{ b = o.b; present = o.present; dlt = o.dlt; o.present = false; return *this; }
				handle_type(const handle_type&) = delete;
				handle_type& operator=(const handle_type&) = delete;

				explicit operator bool() const { return present; }
				raw_handle_type get() const
				{ return present ? const_cast<Dwarf_Block*>(&b) : nullptr; }
				Dwarf_Block *operator->() { return &b; }
				const Dwarf_Block *operator->() const { return &b; }
				deleter& get_deleter() { return dlt; }
				const deleter& get_deleter() const { return dlt; }
			};
			handle_type handle;
			static inline handle_type
			try_construct(const Attribute& a);
			inline explicit Block(const Attribute& a);
			inline Block(handle_type h) : handle(std::move(h)) { /* "upgrade" constructor */
				if (!handle) throw Error(current_dwarf_error, 0);
			}
			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; }
		};

		/* ---- AttributeList --------------------------------------------- *
		 * Built in a single pass with dwarfpp_getattrs (libdw's dwarf_getattrs),
		 * collecting each attribute as an inline-value Attribute into one vector.
		 * Cost: one vector allocation for the whole list, versus the shim model
		 * of a heap wrapper per attribute plus a copy. */
		struct AttributeList
		{
			vector<Attribute> copied_list;
			Die const& d; /* tracked so operator<< / attribute_value can use it */

			/* For the (rarely used) attributes_here() accessor we keep a
			 * handle_type + try_construct; the list itself owns the vector. */
			typedef unique_ptr<vector<Attribute> > handle_type;

			Dwarf_Debug get_dbg() const { return d.get_dbg(); }
			Dwarf_Signed get_len() const { return (Dwarf_Signed) copied_list.size(); }
			Attribute& operator[](Dwarf_Signed i) { return copied_list.at(i); }
			Attribute const& operator[](Dwarf_Signed i) const { return copied_list.at(i); }

			static handle_type try_construct(const Die& it);
			explicit AttributeList(const Die& it);
			AttributeList(handle_type h, const Die& d);
		};

		/* ---- LocdescList ------------------------------------------------ *
		 * A true location list: several Locdescs, each guarding an address range.
		 * Like the single Locdesc, each entry owns its Dwarf_Loc array in a
		 * vector (built in one pass from libdw via dwarfpp_loclist), so the list
		 * is one outer vector of inline-owning handles -- no malloc'd
		 * Dwarf_Locdesc** to dwarf_dealloc. copied_list exposes the same surface
		 * the consumer iterates: each element's get() yields a Dwarf_Locdesc*. */
		struct LocdescList
		{
			typedef Locdesc::handle_type copied_element_type;
			struct handle_type
			{
				vector<copied_element_type> list;
				bool present;
				Debug::raw_handle_type dbg;

				handle_type() : list(), present(false), dbg(nullptr) {}
				handle_type(vector<copied_element_type>&& l, Debug::raw_handle_type dbg)
				 : list(std::move(l)), present(true), dbg(dbg) {}
				handle_type(handle_type&&) = default;
				handle_type& operator=(handle_type&&) = default;
				handle_type(const handle_type&) = delete;
				handle_type& operator=(const handle_type&) = delete;

				explicit operator bool() const { return present; }
			};

			vector<copied_element_type> copied_list;
			Debug::raw_handle_type dbg;
			Debug::raw_handle_type get_dbg() const { return dbg; }

			static inline handle_type
			try_construct(const Attribute& a);

			inline LocdescList(handle_type h)
			 : copied_list(std::move(h.list)), dbg(h.dbg) {}
		};

		/* RangesList owns its synthesised Dwarf_Ranges array in a vector. libdw
		 * yields ranges as (start,end) pairs keyed on the owning DIE, so -- unlike
		 * the hot path -- the libdwarf-shaped array has to be materialised; but it
		 * lives in a single vector, not a malloc'd buffer freed via
		 * dwarf_ranges_dealloc. The handle mimics enough of unique_ptr
		 * (bool, get(), get_deleter().len) for the consuming code, with get()
		 * returning the vector's contiguous storage. */
		struct RangesList
		{
			typedef Dwarf_Ranges *raw_handle_type;
			typedef Dwarf_Ranges raw_element_type;
			struct deleter
			{
				Debug::raw_handle_type dbg;
				Dwarf_Signed len;
				deleter(Debug::raw_handle_type dbg, Dwarf_Signed len) : dbg(dbg), len(len) {}
				/* The vector owns the storage; nothing to release here. */
				void operator()(raw_handle_type) const {}
			};
			struct handle_type
			{
				vector<Dwarf_Ranges> v;
				bool present;
				deleter dlt;

				handle_type() : v(), present(false), dlt(nullptr, 0) {}
				handle_type(std::nullptr_t, const deleter& dl)
				 : v(), present(false), dlt(dl) {}
				handle_type(vector<Dwarf_Ranges>&& vv, const deleter& dl)
				 : v(std::move(vv)), present(true), dlt(dl) {}
				handle_type(handle_type&&) = default;
				handle_type& operator=(handle_type&&) = default;
				handle_type(const handle_type&) = delete;
				handle_type& operator=(const handle_type&) = delete;

				explicit operator bool() const { return present; }
				raw_handle_type get() const
				{ return const_cast<Dwarf_Ranges*>(v.data()); }
				deleter& get_deleter() { return dlt; }
				const deleter& get_deleter() const { return dlt; }
			};
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; }
			handle_type handle;

			static inline handle_type
			try_construct(const Attribute& a, const Die& d);

			RangesList(handle_type h) : handle(std::move(h)) {
				if (!handle) throw Error(current_dwarf_error, 0);
			}
			RangesList(const Attribute& a, const Die& d) : handle(try_construct(a, d))
			{ if (!handle) throw Error(current_dwarf_error, 0); }
		};

		/* srcfiles: a list of file-name strings. The strings belong to libdw, so
		 * we keep plain const char* in a single vector -- no per-string
		 * string_deleter wrapper and no malloc'd char** to dwarf_dealloc. */
		struct StringList
		{
			vector<const char*> copied_list;
			Dwarf_Debug dbg;

			Dwarf_Debug get_dbg() const { return dbg; }
			Dwarf_Signed get_len() const { return (Dwarf_Signed) copied_list.size(); }
			char * operator[](Dwarf_Signed i)       { return const_cast<char*>(copied_list.at(i)); }
			char * operator[](Dwarf_Signed i) const { return const_cast<char*>(copied_list.at(i)); }

			inline explicit StringList(const Die& it);
		};

		// aliases
		typedef LocdescList LocList;
		typedef RangesList RangeList;

		/* ============================ inline impls ====================== */

		inline Attribute::handle_type
		Attribute::try_construct(const Die& h, Dwarf_Half attr)
		{
			Dwarf_Attribute_s returned;
			int ret = dwarfpp_attr(h.raw_handle(), attr, &returned);
			if (ret == DW_DLV_OK) return handle_type(returned, deleter(h.get_dbg()));
			else return handle_type(nullptr, deleter(nullptr));
		}
		inline Attribute::Attribute(const Die& h, Dwarf_Half attr)
		 : handle(try_construct(h, attr))
		{
			if (!this->handle) throw Error(current_dwarf_error, 0);
		}

		inline StringList::StringList(const Die& d)
		 : dbg(d.get_dbg())
		{
			int ret = dwarfpp_srcfiles(d.raw_handle(), copied_list);
			/* DW_DLV_NO_ENTRY -> no source files: a valid, empty list. Only a
			 * real error throws. */
			if (ret == DW_DLV_ERROR) throw Error(current_dwarf_error, 0);
		}

		/* Build a single inline-owning Locdesc handle from a parsed op vector and
		 * its [lopc,hipc) guard. */
		inline Locdesc::handle_type
		locdesc_handle_from(vector<Dwarf_Loc>&& ops, Dwarf_Addr lopc, Dwarf_Addr hipc,
			Dwarf_Debug dbg)
		{
			Dwarf_Locdesc desc{};
			desc.ld_lopc = lopc;
			desc.ld_hipc = hipc;
			desc.ld_from_loclist = 0;
			desc.ld_section_offset = 0;
			return Locdesc::handle_type(std::move(ops), desc, Locdesc::deleter(dbg));
		}

		inline Locdesc::handle_type
		Locdesc::try_construct(const Attribute& a)
		{
			Dwarf_Unsigned exprlen;
			Dwarf_Ptr block_ptr;
			int ret = dwarf_formexprloc(a.handle.get(), &exprlen, &block_ptr,
				&core::current_dwarf_error);
			assert(ret == DW_DLV_OK);

			vector<Dwarf_Loc> ops;
			Dwarf_Addr lopc, hipc;
			ret = dwarfpp_loclist_from_expr(block_ptr, exprlen, ops, &lopc, &hipc);
			if (ret != DW_DLV_OK)
			{
				debug() << "Warning: libdw didn't understand DWARF expression in "
					<< std::hex << "attribute " << DEFAULT_DWARF_SPEC.attr_lookup(a.attr_here()) << std::dec
					<< std::endl;
				return handle_type(nullptr, deleter(a.get_dbg()));
			}
			return locdesc_handle_from(std::move(ops), lopc, hipc, a.get_dbg());
		}

		inline Locdesc::handle_type
		Locdesc::try_construct(Dwarf_Debug dbg, Dwarf_Ptr bytes_in, Dwarf_Unsigned bytes_len)
		{
			vector<Dwarf_Loc> ops;
			Dwarf_Addr lopc, hipc;
			int ret = dwarfpp_loclist_from_expr(bytes_in, bytes_len, ops, &lopc, &hipc);
			if (ret != DW_DLV_OK)
			{
				debug() << "Warning: libdw didn't understand DWARF expression from caller."
					<< std::endl;
				return handle_type(nullptr, deleter(dbg));
			}
			return locdesc_handle_from(std::move(ops), lopc, hipc, dbg);
		}

		inline LocdescList::handle_type
		LocdescList::try_construct(const Attribute& a)
		{
			vector<LoclistEntry> entries;
			int ret = dwarfpp_loclist(a.raw_handle(), entries);
			if (ret != DW_DLV_OK) return handle_type();
			vector<copied_element_type> list;
			list.reserve(entries.size());
			for (auto& e : entries)
			{
				list.push_back(locdesc_handle_from(
					std::move(e.ops), e.lopc, e.hipc, a.get_dbg()));
			}
			return handle_type(std::move(list), a.get_dbg());
		}
		inline RangesList::handle_type
		RangesList::try_construct(const Attribute& a, const Die& d)
		{
			/* libdw keys ranges on the DIE, not on a section offset, so we drive
			 * it from the DIE directly and build the vector in one pass. */
			vector<Dwarf_Ranges> v;
			int ret = dwarfpp_get_ranges(d.raw_handle(), v);
			if (ret == DW_DLV_OK || ret == DW_DLV_NO_ENTRY)
			{
				/* NO_ENTRY -> a valid but empty range list (the consumer simply
				 * iterates zero entries); only a real error throws. */
				Dwarf_Signed n = (Dwarf_Signed) v.size();
				return handle_type(std::move(v), deleter(a.get_dbg(), n));
			}
			return handle_type(nullptr, deleter(nullptr, 0));
		}
		inline Block::handle_type
		Block::try_construct(const Attribute& a)
		{
			Dwarf_Block returned;
			int ret = dwarfpp_formblock(a.raw_handle(), &returned);
			if (ret == DW_DLV_OK)
			{
				return handle_type(returned, deleter(a.get_dbg()));
			} else return handle_type(nullptr, deleter(nullptr));
		}

		inline Block::Block(const Attribute& a) : handle(try_construct(a))
		{
			if (!handle) throw Error(current_dwarf_error, 0);
		}
		std::ostream& operator<<(std::ostream& s, const AttributeList& attrs);
		inline encap::attribute_map Die::copy_attrs() const
		{ return encap::attribute_map(AttributeList(*this), *this, get_constructing_root()); }

	}
}
#endif
