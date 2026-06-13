/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 *
 * libdw.cpp: the C entry points that back libdwarfpp directly with elfutils'
 * libdw (no libdwarf-compatibility shim). See include/dwarfpp/libdw.hpp.
 *
 * Calls into libdw itself are written with a leading "::" so that, inside
 * namespace dwarf::lib, they resolve to elfutils' functions rather than to the
 * wrappers of the same name defined here.
 *
 * Copyright (c) 2008--26, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#include "dwarfpp/libdw.hpp"
#include "dwarfpp/util.hpp"

/* The real elfutils types/functions live here only, kept out of the public
 * header (see libdw.hpp). */
extern "C" {
#include <elfutils/libdw.h>
}

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <type_traits>
#include <vector>
#include <map>
#include <algorithm>
#include <gelf.h>

namespace dwarf
{
	namespace lib
	{
		/* The transparent mirrors declared in libdw.hpp must be byte-for-byte
		 * identical to the libdw structs we reinterpret_cast them to. If a future
		 * elfutils ever changes these layouts, these fire at compile time. */
		static_assert(sizeof(Dwarf_Die_s) == sizeof(::Dwarf_Die),
			"lib::Dwarf_Die_s must match ::Dwarf_Die layout");
		static_assert(alignof(Dwarf_Die_s) == alignof(::Dwarf_Die),
			"lib::Dwarf_Die_s alignment must match ::Dwarf_Die");
		static_assert(sizeof(Dwarf_Attribute_s) == sizeof(::Dwarf_Attribute),
			"lib::Dwarf_Attribute_s must match ::Dwarf_Attribute layout");
		static_assert(alignof(Dwarf_Attribute_s) == alignof(::Dwarf_Attribute),
			"lib::Dwarf_Attribute_s alignment must match ::Dwarf_Attribute");

		/* Reinterpret our inline mirrors as the libdw by-value structs. Safe
		 * given the static_asserts above (POD, identical layout). */
		static inline ::Dwarf_Die *L(Dwarf_Die d)
		{ return reinterpret_cast< ::Dwarf_Die *>(d); }
		static inline ::Dwarf_Attribute *L(Dwarf_Attribute a)
		{ return reinterpret_cast< ::Dwarf_Attribute *>(a); }

		/* Dwarf_Debug stays a heap object (one per file); it carries the ::Dwarf
		 * plus the state we need to emulate libdwarf's stateful CU cursor. */
		struct Dwarf_Debug_s
		{
			::Dwarf *dw;
			::Elf *elf;            // non-null only if we opened it ourselves
			int fd;                // -1 unless we opened it
			::Dwarf_Off next_cu_off;        // arg for the next dwarf_next_unit
			::Dwarf_Off current_cu_die_off; // DIE offset of the current CU
			bool have_current;
		};
		struct Dwarf_Error_s { int errnum; };

		/* NB: default_error_handler(), the thread-local current_dwarf_error and
		 * exception_error_handler() are defined in the backend-neutral
		 * libdwarf.cpp, which is compiled for both backends. */

		/* ---- session / sections ----------------------------------------- */

		/* elfutils' libdw does not apply ELF relocations when reading DWARF from
		 * a relocatable (ET_REL) object. In such objects, inter-section
		 * references in the debug data are left at their on-disk placeholder
		 * (zero), with the real value carried in a RELA entry's addend. The most
		 * damaging case is each CU header's debug_abbrev_offset: left
		 * unrelocated, every CU after the first is matched against the wrong
		 * abbrev table and mis-parsed. We patch the section buffers in place once,
		 * right after opening, before any traversal. x86-64 only. */
		static void apply_debug_relocations(::Elf *e)
		{
			if (!e) return;
			GElf_Ehdr ehdr;
			if (!::gelf_getehdr(e, &ehdr)) return;
			if (ehdr.e_type != ET_REL) return; // linked objects need no patching
			if (ehdr.e_machine != EM_X86_64) return;
			size_t shstrndx = 0;
			if (::elf_getshdrstrndx(e, &shstrndx) != 0) return;
			for (::Elf_Scn *scn = ::elf_nextscn(e, nullptr); scn;
				scn = ::elf_nextscn(e, scn))
			{
				GElf_Shdr sh;
				if (!::gelf_getshdr(scn, &sh)) continue;
				if (sh.sh_type != SHT_RELA || sh.sh_entsize == 0) continue;
				::Elf_Scn *target = ::elf_getscn(e, sh.sh_info);
				GElf_Shdr tsh;
				if (!target || !::gelf_getshdr(target, &tsh)) continue;
				const char *tname = ::elf_strptr(e, shstrndx, tsh.sh_name);
				if (!tname || std::strncmp(tname, ".debug_", 7) != 0) continue;
				::Elf_Data *tdata = ::elf_getdata(target, nullptr);
				if (!tdata || !tdata->d_buf) continue;
				::Elf_Scn *symscn = ::elf_getscn(e, sh.sh_link);
				::Elf_Data *symdata = symscn ? ::elf_getdata(symscn, nullptr) : nullptr;
				if (!symdata) continue;
				::Elf_Data *rdata = ::elf_getdata(scn, nullptr);
				if (!rdata) continue;
				size_t n = sh.sh_size / sh.sh_entsize;
				for (size_t i = 0; i < n; ++i)
				{
					GElf_Rela r;
					if (!::gelf_getrela(rdata, i, &r)) continue;
					GElf_Sym sym;
					if (!::gelf_getsym(symdata, GELF_R_SYM(r.r_info), &sym)) continue;
					uint64_t value = (uint64_t) sym.st_value + (uint64_t) r.r_addend;
					unsigned char *p = (unsigned char *) tdata->d_buf + r.r_offset;
					switch (GELF_R_TYPE(r.r_info))
					{
						case R_X86_64_32:
						case R_X86_64_32S:
							if (r.r_offset + 4 > tdata->d_size) break;
							{ uint32_t v = (uint32_t) value; std::memcpy(p, &v, 4); }
							break;
						case R_X86_64_64:
							if (r.r_offset + 8 > tdata->d_size) break;
							{ uint64_t v = value; std::memcpy(p, &v, 8); }
							break;
						default:
							break;
					}
				}
			}
		}

		int dwarf_init(int fd, int /*access*/, Dwarf_Handler /*errhand*/,
			Dwarf_Ptr /*errarg*/, Dwarf_Debug *ret_dbg, Dwarf_Error * /*error*/)
		{
			/* We open the Elf ourselves with ELF_C_READ (not the mmap'd path
			 * that ::dwarf_begin(fd) uses), because apply_debug_relocations()
			 * patches the section buffers in place and ELF_C_READ gives us
			 * writable, process-private buffers. We then own the Elf and free it
			 * in dwarf_finish. */
			::elf_version(EV_CURRENT);
			::Elf *e = ::elf_begin(fd, ELF_C_READ, nullptr);
			if (!e) return DW_DLV_ERROR;
			apply_debug_relocations(e);
			::Dwarf *dw = ::dwarf_begin_elf(e, DWARF_C_READ, nullptr);
			if (!dw) { ::elf_end(e); return DW_DLV_ERROR; }
			Dwarf_Debug dbg = new Dwarf_Debug_s();
			dbg->dw = dw; dbg->elf = e; dbg->fd = -1;
			dbg->next_cu_off = 0; dbg->current_cu_die_off = 0; dbg->have_current = false;
			*ret_dbg = dbg;
			return DW_DLV_OK;
		}

		int dwarf_elf_init(Elf_opaque_in_libdwarf *elf, int /*access*/,
			Dwarf_Handler /*errhand*/, Dwarf_Ptr /*errarg*/,
			Dwarf_Debug *ret_dbg, Dwarf_Error * /*error*/)
		{
			::Dwarf *dw = ::dwarf_begin_elf(reinterpret_cast< ::Elf*>(elf),
				DWARF_C_READ, nullptr);
			if (!dw) return DW_DLV_ERROR;
			apply_debug_relocations(reinterpret_cast< ::Elf*>(elf));
			Dwarf_Debug dbg = new Dwarf_Debug_s();
			dbg->dw = dw; dbg->elf = nullptr; dbg->fd = -1;
			dbg->next_cu_off = 0; dbg->current_cu_die_off = 0; dbg->have_current = false;
			*ret_dbg = dbg;
			return DW_DLV_OK;
		}

		int dwarf_finish(Dwarf_Debug dbg, Dwarf_Error * /*error*/)
		{
			if (dbg)
			{
				if (dbg->dw) ::dwarf_end(dbg->dw);
				if (dbg->elf) ::elf_end(dbg->elf); // only set if we opened it
				delete dbg;
			}
			return DW_DLV_OK;
		}

		int dwarf_get_elf(Dwarf_Debug dbg, Elf_opaque_in_libdwarf **elf,
			Dwarf_Error * /*error*/)
		{
			::Elf *e = ::dwarf_getelf(dbg->dw);
			if (!e) return DW_DLV_ERROR;
			*elf = reinterpret_cast<Elf_opaque_in_libdwarf*>(e);
			return DW_DLV_OK;
		}

		/* ---- CU / DIE traversal ----------------------------------------- *
		 * libdwarf exposes a stateful cursor: dwarf_next_cu_header_b() steps to
		 * the next CU and the "first DIE of the current CU" is then obtained via
		 * dwarfpp_cu_die(). We emulate that state inside Dwarf_Debug_s. */
		int dwarf_next_cu_header_b(Dwarf_Debug dbg,
			Dwarf_Unsigned *cu_header_length, Dwarf_Half *version_stamp,
			Dwarf_Unsigned *abbrev_offset, Dwarf_Half *address_size,
			Dwarf_Half *offset_size, Dwarf_Half *extension_size,
			Dwarf_Unsigned *next_cu_header_offset, Dwarf_Error * /*error*/)
		{
			::Dwarf_Off next_off = 0;
			size_t header_size = 0;
			::Dwarf_Half version = 0;
			::Dwarf_Off abbrev_off = 0;
			uint8_t addr_size = 0, off_size = 0;
			int r = ::dwarf_next_unit(dbg->dw, dbg->next_cu_off, &next_off,
				&header_size, &version, &abbrev_off, &addr_size, &off_size,
				nullptr, nullptr);
			if (r != 0)
			{
				dbg->next_cu_off = 0;
				dbg->current_cu_die_off = 0;
				dbg->have_current = false;
				return (r < 0) ? DW_DLV_ERROR : DW_DLV_NO_ENTRY;
			}
			dbg->current_cu_die_off = dbg->next_cu_off + header_size;
			dbg->have_current = true;
			if (cu_header_length)  *cu_header_length  = header_size;
			if (version_stamp)     *version_stamp     = version;
			if (abbrev_offset)     *abbrev_offset     = abbrev_off;
			if (address_size)      *address_size      = addr_size;
			if (offset_size)       *offset_size       = off_size;
			if (extension_size)    *extension_size    = 0;
			if (next_cu_header_offset) *next_cu_header_offset = next_off;
			dbg->next_cu_off = next_off;
			return DW_DLV_OK;
		}

		int dwarfpp_cu_die(Dwarf_Debug dbg, Dwarf_Die out)
		{
			if (!dbg->have_current) return DW_DLV_NO_ENTRY;
			if (::dwarf_offdie(dbg->dw, dbg->current_cu_die_off, L(out)) == nullptr)
				return DW_DLV_ERROR;
			return DW_DLV_OK;
		}

		int dwarfpp_siblingof(Dwarf_Debug /*dbg*/, Dwarf_Die die, Dwarf_Die out)
		{
			int r = ::dwarf_siblingof(L(die), L(out));
			if (r == 0) return DW_DLV_OK;
			return (r < 0) ? DW_DLV_ERROR : DW_DLV_NO_ENTRY;
		}

		int dwarfpp_child(Dwarf_Die die, Dwarf_Die out)
		{
			int r = ::dwarf_child(L(die), L(out));
			if (r == 0) return DW_DLV_OK;
			return (r < 0) ? DW_DLV_ERROR : DW_DLV_NO_ENTRY;
		}

		int dwarfpp_offdie(Dwarf_Debug dbg, Dwarf_Off off, Dwarf_Die out)
		{
			if (::dwarf_offdie(dbg->dw, off, L(out)) == nullptr) return DW_DLV_ERROR;
			return DW_DLV_OK;
		}

		/* ---- DIE accessors ---------------------------------------------- */
		int dwarf_dieoffset(Dwarf_Die die, Dwarf_Off *ret_off, Dwarf_Error * /*error*/)
		{
			*ret_off = ::dwarf_dieoffset(L(die));
			return DW_DLV_OK;
		}

		int dwarf_tag(Dwarf_Die die, Dwarf_Half *ret_tag, Dwarf_Error * /*error*/)
		{
			*ret_tag = (Dwarf_Half) ::dwarf_tag(L(die));
			return DW_DLV_OK;
		}

		int dwarf_diename(Dwarf_Die die, char **ret_name, Dwarf_Error * /*error*/)
		{
			const char *n = ::dwarf_diename(L(die));
			if (!n) return DW_DLV_NO_ENTRY;
			*ret_name = const_cast<char*>(n);
			return DW_DLV_OK;
		}

		int dwarf_hasattr(Dwarf_Die die, Dwarf_Half attr, Dwarf_Bool *ret_bool,
			Dwarf_Error * /*error*/)
		{
			*ret_bool = ::dwarf_hasattr(L(die), attr) ? 1 : 0;
			return DW_DLV_OK;
		}

		int dwarf_CU_dieoffset_given_die(Dwarf_Die die, Dwarf_Off *ret_off,
			Dwarf_Error * /*error*/)
		{
			::Dwarf_Die cu;
			if (::dwarf_diecu(L(die), &cu, nullptr, nullptr) == nullptr)
				return DW_DLV_ERROR;
			*ret_off = ::dwarf_dieoffset(&cu);
			return DW_DLV_OK;
		}

		/* ---- attribute access ------------------------------------------- */
		int dwarfpp_attr(Dwarf_Die die, Dwarf_Half attr, Dwarf_Attribute out)
		{
			if (::dwarf_attr(L(die), attr, L(out)) == nullptr) return DW_DLV_NO_ENTRY;
			return DW_DLV_OK;
		}

		namespace {
			struct getattrs_trampoline { int (*cb)(Dwarf_Attribute, void*); void *arg; };
			int getattrs_thunk(::Dwarf_Attribute *a, void *p)
			{
				getattrs_trampoline *t = static_cast<getattrs_trampoline*>(p);
				return t->cb(reinterpret_cast<Dwarf_Attribute>(a), t->arg);
			}
		}

		int dwarfpp_getattrs(Dwarf_Die die,
			int (*cb)(Dwarf_Attribute, void *), void *arg)
		{
			getattrs_trampoline t { cb, arg };
			/* Our thunk forwards the user's return code (DWARF_CB_OK to continue);
			 * dwarf_getattrs returns a negative offset on error, else the offset
			 * at which iteration stopped. */
			ptrdiff_t off = ::dwarf_getattrs(L(die), getattrs_thunk, &t, 0);
			return (off < 0) ? DW_DLV_ERROR : DW_DLV_OK;
		}

		int dwarf_whatattr(Dwarf_Attribute attr, Dwarf_Half *ret_attr,
			Dwarf_Error * /*error*/)
		{
			*ret_attr = (Dwarf_Half) ::dwarf_whatattr(L(attr));
			return DW_DLV_OK;
		}

		int dwarf_whatform(Dwarf_Attribute attr, Dwarf_Half *ret_form,
			Dwarf_Error * /*error*/)
		{
			*ret_form = (Dwarf_Half) ::dwarf_whatform(L(attr));
			return DW_DLV_OK;
		}

		/* ---- form value extraction -------------------------------------- */
		int dwarf_formstring(Dwarf_Attribute attr, char **ret, Dwarf_Error * /*error*/)
		{
			const char *s = ::dwarf_formstring(L(attr));
			if (!s) return DW_DLV_ERROR;
			*ret = const_cast<char*>(s);
			return DW_DLV_OK;
		}

		int dwarf_formflag(Dwarf_Attribute attr, Dwarf_Bool *ret, Dwarf_Error * /*error*/)
		{
			bool b = false;
			if (::dwarf_formflag(L(attr), &b) != 0) return DW_DLV_ERROR;
			*ret = b ? 1 : 0;
			return DW_DLV_OK;
		}

		int dwarf_formaddr(Dwarf_Attribute attr, Dwarf_Addr *ret, Dwarf_Error * /*error*/)
		{
			::Dwarf_Addr a = 0;
			if (::dwarf_formaddr(L(attr), &a) != 0) return DW_DLV_ERROR;
			*ret = a;
			return DW_DLV_OK;
		}

		int dwarf_formudata(Dwarf_Attribute attr, Dwarf_Unsigned *ret, Dwarf_Error * /*error*/)
		{
			::Dwarf_Word w = 0;
			if (::dwarf_formudata(L(attr), &w) != 0) return DW_DLV_ERROR;
			*ret = w;
			return DW_DLV_OK;
		}

		int dwarf_formsdata(Dwarf_Attribute attr, Dwarf_Signed *ret, Dwarf_Error * /*error*/)
		{
			::Dwarf_Sword w = 0;
			if (::dwarf_formsdata(L(attr), &w) != 0) return DW_DLV_ERROR;
			*ret = w;
			return DW_DLV_OK;
		}

		int dwarf_global_formref(Dwarf_Attribute attr, Dwarf_Off *ret, Dwarf_Error * /*error*/)
		{
			/* Reference forms (DW_FORM_ref*) resolve to a DIE; section-offset
			 * forms (DW_FORM_sec_offset, data4/8) are read as a raw value. */
			::Dwarf_Die d;
			if (::dwarf_formref_die(L(attr), &d) != nullptr)
			{
				*ret = ::dwarf_dieoffset(&d);
				return DW_DLV_OK;
			}
			::Dwarf_Word w = 0;
			if (::dwarf_formudata(L(attr), &w) == 0)
			{
				*ret = w;
				return DW_DLV_OK;
			}
			return DW_DLV_ERROR;
		}

		/* A block's bytes live in the section data that the ::Dwarf owns, so we
		 * just copy libdw's two fields into the caller's inline Dwarf_Block --
		 * no heap allocation, nothing to dwarf_dealloc. */
		int dwarfpp_formblock(Dwarf_Attribute attr, Dwarf_Block *out)
		{
			::Dwarf_Block blk;
			if (::dwarf_formblock(L(attr), &blk) != 0) return DW_DLV_ERROR;
			out->bl_len = blk.length;
			out->bl_data = blk.data;
			out->bl_from_loclist = 0;
			out->bl_section_offset = 0;
			return DW_DLV_OK;
		}

		/* ---- deallocation / errors -------------------------------------- *
		 * In the direct model nothing the handle layer hands back is heap-
		 * allocated by us: dies, attributes and blocks are held by value, and the
		 * cold paths (loclists, ranges, srcfiles) own their results in C++
		 * containers. dwarf_dealloc is thus a no-op shim, retained because the
		 * handle deleters and string_deleter still call it (e.g. DW_DLA_STRING,
		 * whose storage libdw owns). */
		void dwarf_dealloc(Dwarf_Debug /*dbg*/, void * /*space*/, Dwarf_Unsigned /*type*/)
		{
		}

		const char *dwarf_errmsg(Dwarf_Error /*error*/)
		{
			const char *m = ::dwarf_errmsg(-1);
			return m ? m : "(no libdw error)";
		}

		int dwarf_errno(Dwarf_Error /*error*/)
		{
			return ::dwarf_errno();
		}

		/* ================================================================== *
		 *  Location lists and range lists, over libdw.                        *
		 * ================================================================== */
		namespace {
			/* Fill a Dwarf_Loc vector from libdw's parsed Dwarf_Op[]. The two
			 * structs are field-for-field equivalent. */
			void ops_to_loc_vector(const ::Dwarf_Op *ops, size_t n,
				std::vector<Dwarf_Loc>& out)
			{
				out.resize(n);
				for (size_t i = 0; i < n; ++i)
				{
					out[i].lr_atom    = ops[i].atom;
					out[i].lr_number  = ops[i].number;
					out[i].lr_number2 = ops[i].number2;
					out[i].lr_offset  = ops[i].offset;
				}
			}

			/* Minimal LEB128 readers over a byte buffer. */
			uint64_t read_uleb(const unsigned char *p, size_t len, size_t &i)
			{
				uint64_t r = 0; unsigned shift = 0;
				while (i < len)
				{
					unsigned char b = p[i++];
					r |= uint64_t(b & 0x7f) << shift;
					if (!(b & 0x80)) break;
					shift += 7;
				}
				return r;
			}
			int64_t read_sleb(const unsigned char *p, size_t len, size_t &i)
			{
				int64_t r = 0; unsigned shift = 0; unsigned char b = 0;
				while (i < len)
				{
					b = p[i++];
					r |= int64_t(b & 0x7f) << shift;
					shift += 7;
					if (!(b & 0x80)) break;
				}
				if (shift < 64 && (b & 0x40)) r |= -(int64_t(1) << shift);
				return r;
			}
			template <typename T> uint64_t read_fixed(const unsigned char *p, size_t len, size_t &i)
			{
				T v = 0;
				if (i + sizeof(T) <= len) { memcpy(&v, p + i, sizeof(T)); i += sizeof(T); }
				return (uint64_t)v;
			}

			/* Decode a DWARF location/CFA expression's bytes into Dwarf_Locs.
			 * Returns false if an unrecognised opcode is hit (whose operand
			 * length we cannot know), so the caller can degrade gracefully. */
			bool parse_expr(const unsigned char *p, size_t len,
				unsigned addr_size, unsigned offset_size, std::vector<Dwarf_Loc> &out)
			{
				size_t i = 0;
				while (i < len)
				{
					Dwarf_Loc loc; memset(&loc, 0, sizeof loc);
					loc.lr_offset = i;
					unsigned char op = p[i++];
					loc.lr_atom = op;
					if (op >= DW_OP_lit0 && op <= DW_OP_lit31) { /* no operand */ }
					else if (op >= DW_OP_reg0 && op <= DW_OP_reg31) { /* no operand */ }
					else if (op >= DW_OP_breg0 && op <= DW_OP_breg31)
						loc.lr_number = (Dwarf_Unsigned) read_sleb(p, len, i);
					else switch (op)
					{
						case DW_OP_deref: case DW_OP_dup: case DW_OP_drop:
						case DW_OP_over: case DW_OP_swap: case DW_OP_rot:
						case DW_OP_xderef: case DW_OP_abs: case DW_OP_and:
						case DW_OP_div: case DW_OP_minus: case DW_OP_mod:
						case DW_OP_mul: case DW_OP_neg: case DW_OP_not:
						case DW_OP_or: case DW_OP_plus: case DW_OP_shl:
						case DW_OP_shr: case DW_OP_shra: case DW_OP_xor:
						case DW_OP_eq: case DW_OP_ge: case DW_OP_gt:
						case DW_OP_le: case DW_OP_lt: case DW_OP_ne:
						case DW_OP_nop: case DW_OP_push_object_address:
						case DW_OP_form_tls_address: case DW_OP_call_frame_cfa:
						case DW_OP_stack_value:
							break;
						case DW_OP_const1u: case DW_OP_pick:
						case DW_OP_deref_size: case DW_OP_xderef_size:
							loc.lr_number = read_fixed<uint8_t>(p, len, i); break;
						case DW_OP_const1s:
							loc.lr_number = (Dwarf_Unsigned)(int64_t)(int8_t)read_fixed<uint8_t>(p, len, i); break;
						case DW_OP_const2u: case DW_OP_call2:
							loc.lr_number = read_fixed<uint16_t>(p, len, i); break;
						case DW_OP_const2s: case DW_OP_skip: case DW_OP_bra:
							loc.lr_number = (Dwarf_Unsigned)(int64_t)(int16_t)read_fixed<uint16_t>(p, len, i); break;
						case DW_OP_const4u: case DW_OP_call4:
							loc.lr_number = read_fixed<uint32_t>(p, len, i); break;
						case DW_OP_const4s:
							loc.lr_number = (Dwarf_Unsigned)(int64_t)(int32_t)read_fixed<uint32_t>(p, len, i); break;
						case DW_OP_const8u: case DW_OP_const8s:
							loc.lr_number = read_fixed<uint64_t>(p, len, i); break;
						case DW_OP_addr:
							loc.lr_number = (addr_size == 4)
								? read_fixed<uint32_t>(p, len, i)
								: read_fixed<uint64_t>(p, len, i);
							break;
						case DW_OP_call_ref:
							loc.lr_number = (offset_size == 8)
								? read_fixed<uint64_t>(p, len, i)
								: read_fixed<uint32_t>(p, len, i);
							break;
						case DW_OP_constu: case DW_OP_plus_uconst: case DW_OP_regx:
						case DW_OP_piece: case DW_OP_addrx: case DW_OP_constx:
							loc.lr_number = (Dwarf_Unsigned) read_uleb(p, len, i); break;
						case DW_OP_consts: case DW_OP_fbreg:
							loc.lr_number = (Dwarf_Unsigned) read_sleb(p, len, i); break;
						case DW_OP_bregx:
							loc.lr_number  = (Dwarf_Unsigned) read_uleb(p, len, i);
							loc.lr_number2 = (Dwarf_Unsigned) read_sleb(p, len, i);
							break;
						case DW_OP_bit_piece:
							loc.lr_number  = (Dwarf_Unsigned) read_uleb(p, len, i);
							loc.lr_number2 = (Dwarf_Unsigned) read_uleb(p, len, i);
							break;
						case DW_OP_implicit_value:
						case DW_OP_entry_value:
						{
							uint64_t l = read_uleb(p, len, i);
							loc.lr_number = (Dwarf_Unsigned) l;
							i += l;
							break;
						}
						default:
							return false;
					}
					if (i > len) return false;
					out.push_back(loc);
				}
				return true;
			}
		} // anonymous namespace

		/* A true location list: each entry is a parsed op vector plus its
		 * [lopc,hipc) guard. The handle layer materialises each as an inline
		 * Dwarf_Locdesc, so we hand back the parsed ops directly with no malloc. */
		int dwarfpp_loclist(Dwarf_Attribute attr, std::vector<LoclistEntry>& out)
		{
			out.clear();
			ptrdiff_t off = 0;
			::Dwarf_Addr base = 0, start = 0, end = 0;
			::Dwarf_Op *ops = nullptr; size_t nops = 0;
			while ((off = ::dwarf_getlocations(L(attr), off, &base, &start, &end,
				&ops, &nops)) > 0)
			{
				LoclistEntry e;
				e.lopc = start; e.hipc = end;
				ops_to_loc_vector(ops, nops, e.ops);
				out.push_back(std::move(e));
			}
			if (off < 0) { out.clear(); return DW_DLV_ERROR; }
			if (out.empty()) return DW_DLV_NO_ENTRY;
			return DW_DLV_OK;
		}

		/* A bare location expression (DW_FORM_exprloc, or raw caller bytes): decode
		 * the operations into the caller's vector. It is valid for the entire
		 * address range, which we report as libdwarf does: lopc = 0, hipc = ~0. */
		int dwarfpp_loclist_from_expr(Dwarf_Ptr bytes_in, Dwarf_Unsigned bytes_len,
			std::vector<Dwarf_Loc>& out_ops, Dwarf_Addr *out_lopc, Dwarf_Addr *out_hipc)
		{
			out_ops.clear();
			if (!parse_expr(static_cast<const unsigned char*>(bytes_in),
				(size_t) bytes_len, /*addr_size*/8, /*offset_size*/4, out_ops))
			{
				out_ops.clear();
				return DW_DLV_ERROR; /* unrecognised opcode -> caller degrades */
			}
			if (out_lopc) *out_lopc = 0;
			if (out_hipc) *out_hipc = (Dwarf_Addr) -1;
			return DW_DLV_OK;
		}

		int dwarf_formexprloc(Dwarf_Attribute attr, Dwarf_Unsigned *ret_exprlen,
			Dwarf_Ptr *block_ptr, Dwarf_Error * /*error*/)
		{
			::Dwarf_Block blk;
			if (::dwarf_formblock(L(attr), &blk) != 0) return DW_DLV_NO_ENTRY;
			if (ret_exprlen) *ret_exprlen = blk.length;
			if (block_ptr) *block_ptr = blk.data;
			return DW_DLV_OK;
		}

		/* Ranges: libdw yields ranges as (start,end) pairs keyed on the owning
		 * DIE, so the libdwarf-shaped array must be materialised. We build it
		 * straight into the caller's vector (one allocation, freed by the
		 * handle's vector) rather than malloc'ing a buffer to dwarf_ranges_dealloc.
		 * The trailing DW_RANGES_END entry mirrors libdwarf's terminator. */
		int dwarfpp_get_ranges(Dwarf_Die die, std::vector<Dwarf_Ranges>& out)
		{
			out.clear();
			if (!die) return DW_DLV_NO_ENTRY;
			ptrdiff_t off = 0;
			::Dwarf_Addr base = 0, start = 0, end = 0;
			while ((off = ::dwarf_ranges(L(die), off, &base, &start, &end)) > 0)
			{
				Dwarf_Ranges r;
				r.dwr_addr1 = start; r.dwr_addr2 = end; r.dwr_type = DW_RANGES_ENTRY;
				out.push_back(r);
			}
			if (off < 0) { out.clear(); return DW_DLV_ERROR; }
			if (out.empty()) return DW_DLV_NO_ENTRY;
			Dwarf_Ranges term; term.dwr_addr1 = 0; term.dwr_addr2 = 0;
			term.dwr_type = DW_RANGES_END;
			out.push_back(term);
			return DW_DLV_OK;
		}
		/* srcfiles: the file-name strings belong to libdw, so we hand back plain
		 * const char* into the caller's vector -- no malloc'd char** to free.
		 * libdwarfpp indexes this as names[decl_file - 1], i.e. element k is
		 * DWARF file number k+1; libdw's dwarf_filesrc takes the file number
		 * directly (index 0 reserved), so element k maps to file k + 1. */
		int dwarfpp_srcfiles(Dwarf_Die die, std::vector<const char*>& out)
		{
			out.clear();
			::Dwarf_Files *files = nullptr; size_t n = 0;
			if (::dwarf_getsrcfiles(L(die), &files, &n) != 0) return DW_DLV_ERROR;
			if (n <= 1) return DW_DLV_NO_ENTRY;
			for (size_t i = 1; i < n; ++i)
			{
				const char *s = ::dwarf_filesrc(files, i, nullptr, nullptr);
				out.push_back(s ? s : "");
			}
			return DW_DLV_OK;
		}

		/* ================================================================== *
		 *  Frame / CFI, over libdw's dwarf_next_cfi (.debug_frame/.eh_frame). *
		 *                                                                     *
		 *  libdwarfpp's FrameSection iterates raw CIEs/FDEs and interprets    *
		 *  the instruction streams itself, so we parse the frame section with *
		 *  dwarf_next_cfi -- which frames each entry and points into the      *
		 *  section data -- and present the result as libdwarf's FDE/CIE API.  *
		 *  Cie/Fde handles are pointers into structures we own; equality is   *
		 *  pointer identity, as libdwarfpp expects.                           *
		 * ================================================================== */

		namespace {
			uint64_t read_uN(const uint8_t *&p, const uint8_t *end, unsigned n)
			{
				uint64_t v = 0;
				for (unsigned i = 0; i < n && p < end; ++i) v |= uint64_t(*p++) << (8 * i);
				return v;
			}
			uint64_t uleb_p(const uint8_t *&p, const uint8_t *end)
			{
				uint64_t r = 0; unsigned s = 0;
				while (p < end) { uint8_t b = *p++; r |= uint64_t(b & 0x7f) << s; if (!(b & 0x80)) break; s += 7; }
				return r;
			}
			int64_t sleb_p(const uint8_t *&p, const uint8_t *end)
			{
				int64_t r = 0; unsigned s = 0; uint8_t b = 0;
				while (p < end) { b = *p++; r |= int64_t(b & 0x7f) << s; s += 7; if (!(b & 0x80)) break; }
				if (s < 64 && (b & 0x40)) r |= -(int64_t(1) << s);
				return r;
			}
			int64_t sign_extend(uint64_t v, unsigned bytes)
			{
				unsigned bits = bytes * 8;
				if (bits < 64 && (v & (uint64_t(1) << (bits - 1)))) v |= ~((uint64_t(1) << bits) - 1);
				return (int64_t) v;
			}
			/* Read a DW_EH_PE-encoded value (the format part only), advancing p. */
			uint64_t read_eh_value(unsigned char enc, const uint8_t *&p, const uint8_t *end,
				unsigned addr_size)
			{
				switch (enc & 0x0f)
				{
					case DW_EH_PE_absptr:  return read_uN(p, end, addr_size);
					case DW_EH_PE_uleb128: return uleb_p(p, end);
					case DW_EH_PE_sleb128: return (uint64_t) sleb_p(p, end);
					case DW_EH_PE_udata2:  return read_uN(p, end, 2);
					case DW_EH_PE_udata4:  return read_uN(p, end, 4);
					case DW_EH_PE_udata8:  return read_uN(p, end, 8);
					case DW_EH_PE_sdata2:  return (uint64_t) sign_extend(read_uN(p, end, 2), 2);
					case DW_EH_PE_sdata4:  return (uint64_t) sign_extend(read_uN(p, end, 4), 4);
					case DW_EH_PE_sdata8:  return read_uN(p, end, 8);
					default:               return 0;
				}
			}
			/* Read a DW_EH_PE-encoded pointer, applying the base for pcrel.
			 * value_vaddr is the run-time address of the encoded value itself. */
			uint64_t read_eh_pointer(unsigned char enc, const uint8_t *&p, const uint8_t *end,
				uint64_t value_vaddr, unsigned addr_size)
			{
				if (enc == 0xff /* DW_EH_PE_omit */) return 0;
				uint64_t raw = read_eh_value(enc, p, end, addr_size);
				switch (enc & 0x70)
				{
					case DW_EH_PE_pcrel: return value_vaddr + raw;
					default:             return raw; /* absptr (and best-effort others) */
				}
			}
			/* Determine the FDE pointer encoding ('R') from a CIE augmentation. */
			unsigned char cie_fde_encoding(const char *aug, const uint8_t *augdata,
				size_t augsz, unsigned addr_size)
			{
				if (!aug || aug[0] != 'z') return DW_EH_PE_absptr;
				const uint8_t *p = augdata, *end = augdata + augsz;
				for (const char *c = aug + 1; *c; ++c)
				{
					switch (*c)
					{
						case 'R': return (p < end) ? *p : (unsigned char) DW_EH_PE_absptr;
						case 'L': if (p < end) ++p; break;
						case 'S': break;
						case 'P': {
							if (p >= end) return DW_EH_PE_absptr;
							unsigned char penc = *p++;
							read_eh_value(penc, p, end, addr_size); /* skip personality ptr */
						} break;
						default: return DW_EH_PE_absptr;
					}
				}
				return DW_EH_PE_absptr;
			}
		} // anonymous namespace

		/* Full definitions of the opaque CFI handle types. */
		struct Dwarf_Cie_s {
			Dwarf_Off offset;
			Dwarf_Unsigned bytes_in_cie;
			Dwarf_Small version;
			char *augmenter;                  /* points into section data */
			Dwarf_Unsigned code_alignment_factor;
			Dwarf_Signed data_alignment_factor;
			Dwarf_Half return_address_register;
			Dwarf_Ptr initial_instructions;   /* points into section data */
			Dwarf_Unsigned initial_instructions_length;
			Dwarf_Signed index;
			unsigned char fde_encoding;
			size_t fde_aug_data_size;
			bool has_z;
		};
		struct Dwarf_Fde_s {
			Dwarf_Off offset;
			Dwarf_Addr low_pc;
			Dwarf_Unsigned func_length;
			Dwarf_Ptr fde_bytes;              /* points into section data */
			Dwarf_Unsigned fde_byte_length;
			Dwarf_Ptr instr_bytes;            /* points into section data */
			Dwarf_Unsigned instr_len;
			Dwarf_Cie cie;
			Dwarf_Signed cie_index;
			Dwarf_Off cie_offset_field;       /* value for dwarf_get_fde_range */
		};

		static int build_fde_list(Dwarf_Debug dbg, bool eh,
			Dwarf_Cie **cie_data, Dwarf_Signed *cie_count,
			Dwarf_Fde **fde_data, Dwarf_Signed *fde_count)
		{
			::Elf *elf = ::dwarf_getelf(dbg->dw);
			if (!elf) return DW_DLV_NO_ENTRY;
			size_t shstrndx = 0;
			if (::elf_getshdrstrndx(elf, &shstrndx) != 0) return DW_DLV_NO_ENTRY;
			const char *want = eh ? ".eh_frame" : ".debug_frame";
			Elf_Scn *scn = nullptr; Elf_Data *data = nullptr;
			::GElf_Addr sh_addr = 0; bool found = false;
			for (Elf_Scn *s = nullptr; (s = ::elf_nextscn(elf, s)); )
			{
				::GElf_Shdr sh;
				if (!::gelf_getshdr(s, &sh)) continue;
				const char *nm = ::elf_strptr(elf, shstrndx, sh.sh_name);
				if (nm && strcmp(nm, want) == 0) { scn = s; sh_addr = sh.sh_addr; found = true; break; }
			}
			if (!found) return DW_DLV_NO_ENTRY;
			data = ::elf_getdata(scn, nullptr);
			if (!data || !data->d_buf) return DW_DLV_NO_ENTRY;
			const uint8_t *sec = static_cast<const uint8_t*>(data->d_buf);
			unsigned char *eident = reinterpret_cast<unsigned char*>(::elf_getident(elf, nullptr));
			unsigned addr_size = (eident && eident[EI_CLASS] == ELFCLASS64) ? 8 : 4;

			std::vector<Dwarf_Cie_s*> cies;
			std::vector<Dwarf_Fde_s*> fdes;
			std::map< ::Dwarf_Off, Dwarf_Cie_s*> cie_by_off;

			::Dwarf_Off off = 0, next = 0;
			::Dwarf_CFI_Entry entry;
			int r;
			while ((r = ::dwarf_next_cfi(eident, data, eh, off, &next, &entry)) == 0)
			{
				if (dwarf_cfi_cie_p(&entry))
				{
					Dwarf_Cie_s *c = new Dwarf_Cie_s();
					c->offset = off;
					c->augmenter = const_cast<char*>(entry.cie.augmentation ? entry.cie.augmentation : "");
					c->version = entry.cie.augmentation
						? *(reinterpret_cast<const uint8_t*>(entry.cie.augmentation) - 1) : 1;
					c->code_alignment_factor = entry.cie.code_alignment_factor;
					c->data_alignment_factor = entry.cie.data_alignment_factor;
					c->return_address_register = (Dwarf_Half) entry.cie.return_address_register;
					c->initial_instructions = const_cast<uint8_t*>(entry.cie.initial_instructions);
					c->initial_instructions_length =
						entry.cie.initial_instructions_end - entry.cie.initial_instructions;
					bool is64 = (sec + off + 4 <= sec + data->d_size)
						&& (*reinterpret_cast<const uint32_t*>(sec + off) == 0xffffffffu);
					c->bytes_in_cie = (Dwarf_Unsigned)(next - off) - (is64 ? 12 : 4);
					c->index = (Dwarf_Signed) cies.size();
					c->fde_encoding = eh
						? cie_fde_encoding(c->augmenter, entry.cie.augmentation_data,
							entry.cie.augmentation_data_size, addr_size)
						: (unsigned char) DW_EH_PE_absptr;
					c->fde_aug_data_size = entry.cie.fde_augmentation_data_size;
					c->has_z = (c->augmenter[0] == 'z');
					cies.push_back(c);
					cie_by_off[off] = c;
				}
				else
				{
					Dwarf_Fde_s *f = new Dwarf_Fde_s();
					f->offset = off;
					::Dwarf_Off abs_cie_off = entry.fde.CIE_pointer;
					auto found_cie = cie_by_off.find(abs_cie_off);
					Dwarf_Cie_s *c = (found_cie != cie_by_off.end()) ? found_cie->second : nullptr;
					f->cie = c;
					f->cie_index = c ? c->index : 0;
					const uint8_t *p = entry.fde.start;
					const uint8_t *pend = entry.fde.end;
					unsigned char enc = c ? c->fde_encoding : (unsigned char) DW_EH_PE_absptr;
					uint64_t loc_vaddr = sh_addr + (uint64_t)(p - sec);
					f->low_pc = read_eh_pointer(enc, p, pend, loc_vaddr, addr_size);
					/* address_range is a length: same format, no base applied. */
					f->func_length = read_eh_value(enc, p, pend, addr_size);
					/* skip the FDE's augmentation data to reach the instructions */
					if (c && c->has_z) { uint64_t al = uleb_p(p, pend); p += al; }
					else if (c) p += c->fde_aug_data_size;
					if (p > pend) p = pend;
					f->instr_bytes = const_cast<uint8_t*>(p);
					f->instr_len = (Dwarf_Unsigned)(pend - p);
					f->fde_bytes = const_cast<uint8_t*>(sec + off);
					bool fde_is64 = (sec + off + 4 <= sec + data->d_size)
						&& (*reinterpret_cast<const uint32_t*>(sec + off) == 0xffffffffu);
					f->fde_byte_length = (Dwarf_Unsigned)(next - off) - (fde_is64 ? 12 : 4);
					/* libdwarf reports, for .eh_frame, the *relative* CIE pointer
					 * that FrameSection turns back into an absolute offset; for
					 * .debug_frame it is already absolute. */
					f->cie_offset_field = eh
						? (Dwarf_Off)(off + 4) - abs_cie_off
						: abs_cie_off;
					fdes.push_back(f);
				}
				off = next;
			}

			/* libdwarfpp's FrameSection requires the FDE array sorted by initial
			 * location. A linked .eh_frame is not necessarily in that order, so
			 * sort here, as libdwarf's FDE list effectively did. */
			std::sort(fdes.begin(), fdes.end(),
				[](const Dwarf_Fde_s *a, const Dwarf_Fde_s *b) {
					return a->low_pc < b->low_pc;
				});

			*cie_count = (Dwarf_Signed) cies.size();
			*fde_count = (Dwarf_Signed) fdes.size();
			Dwarf_Cie *ca = static_cast<Dwarf_Cie*>(malloc((cies.size() + 1) * sizeof(Dwarf_Cie)));
			for (size_t i = 0; i < cies.size(); ++i) ca[i] = cies[i];
			ca[cies.size()] = nullptr;
			Dwarf_Fde *fa = static_cast<Dwarf_Fde*>(malloc((fdes.size() + 1) * sizeof(Dwarf_Fde)));
			for (size_t i = 0; i < fdes.size(); ++i) fa[i] = fdes[i];
			fa[fdes.size()] = nullptr;
			*cie_data = ca; *fde_data = fa;
			if (cies.empty() && fdes.empty()) return DW_DLV_NO_ENTRY;
			return DW_DLV_OK;
		}

		int dwarf_get_fde_list(Dwarf_Debug dbg, Dwarf_Cie **cie_data,
			Dwarf_Signed *cie_count, Dwarf_Fde **fde_data,
			Dwarf_Signed *fde_count, Dwarf_Error * /*error*/)
		{ return build_fde_list(dbg, false, cie_data, cie_count, fde_data, fde_count); }

		int dwarf_get_fde_list_eh(Dwarf_Debug dbg, Dwarf_Cie **cie_data,
			Dwarf_Signed *cie_count, Dwarf_Fde **fde_data,
			Dwarf_Signed *fde_count, Dwarf_Error * /*error*/)
		{ return build_fde_list(dbg, true, cie_data, cie_count, fde_data, fde_count); }

		void dwarf_fde_cie_list_dealloc(Dwarf_Debug, Dwarf_Cie *cie_data,
			Dwarf_Signed cie_count, Dwarf_Fde *fde_data, Dwarf_Signed fde_count)
		{
			if (cie_data) { for (Dwarf_Signed i = 0; i < cie_count; ++i) delete cie_data[i]; free(cie_data); }
			if (fde_data) { for (Dwarf_Signed i = 0; i < fde_count; ++i) delete fde_data[i]; free(fde_data); }
		}

		int dwarf_get_fde_range(Dwarf_Fde fde, Dwarf_Addr *low_pc,
			Dwarf_Unsigned *func_length, Dwarf_Ptr *fde_bytes,
			Dwarf_Unsigned *fde_byte_length, Dwarf_Off *cie_offset,
			Dwarf_Signed *cie_index, Dwarf_Off *fde_offset, Dwarf_Error * /*error*/)
		{
			if (!fde) return DW_DLV_NO_ENTRY;
			if (low_pc)          *low_pc = fde->low_pc;
			if (func_length)     *func_length = fde->func_length;
			if (fde_bytes)       *fde_bytes = fde->fde_bytes;
			if (fde_byte_length) *fde_byte_length = fde->fde_byte_length;
			if (cie_offset)      *cie_offset = fde->cie_offset_field;
			if (cie_index)       *cie_index = fde->cie_index;
			if (fde_offset)      *fde_offset = fde->offset;
			return DW_DLV_OK;
		}

		int dwarf_get_cie_of_fde(Dwarf_Fde fde, Dwarf_Cie *cie_returned, Dwarf_Error * /*error*/)
		{
			if (!fde || !fde->cie) return DW_DLV_NO_ENTRY;
			*cie_returned = fde->cie;
			return DW_DLV_OK;
		}

		int dwarf_get_cie_info(Dwarf_Cie cie, Dwarf_Unsigned *bytes_in_cie,
			Dwarf_Small *version, char **augmenter,
			Dwarf_Unsigned *code_alignment_factor, Dwarf_Signed *data_alignment_factor,
			Dwarf_Half *return_address_register_rule, Dwarf_Ptr *initial_instructions,
			Dwarf_Unsigned *initial_instructions_length, Dwarf_Error * /*error*/)
		{
			if (!cie) return DW_DLV_NO_ENTRY;
			if (bytes_in_cie)                 *bytes_in_cie = cie->bytes_in_cie;
			if (version)                      *version = cie->version;
			if (augmenter)                    *augmenter = cie->augmenter;
			if (code_alignment_factor)        *code_alignment_factor = cie->code_alignment_factor;
			if (data_alignment_factor)        *data_alignment_factor = cie->data_alignment_factor;
			if (return_address_register_rule) *return_address_register_rule = cie->return_address_register;
			if (initial_instructions)         *initial_instructions = cie->initial_instructions;
			if (initial_instructions_length)  *initial_instructions_length = cie->initial_instructions_length;
			return DW_DLV_OK;
		}

		int dwarf_get_cie_index(Dwarf_Cie cie, Dwarf_Signed *index, Dwarf_Error * /*error*/)
		{
			if (!cie) return DW_DLV_NO_ENTRY;
			*index = cie->index;
			return DW_DLV_OK;
		}

		int dwarf_get_fde_instr_bytes(Dwarf_Fde fde, Dwarf_Ptr *outinstrs,
			Dwarf_Unsigned *outlen, Dwarf_Error * /*error*/)
		{
			if (!fde) return DW_DLV_NO_ENTRY;
			*outinstrs = fde->instr_bytes;
			*outlen = fde->instr_len;
			return DW_DLV_OK;
		}

		int dwarf_get_fde_at_pc(Dwarf_Fde *fde_data, Dwarf_Addr pc,
			Dwarf_Fde *returned_fde, Dwarf_Addr *lopc, Dwarf_Addr *hipc, Dwarf_Error * /*error*/)
		{
			if (!fde_data) return DW_DLV_NO_ENTRY;
			for (Dwarf_Fde *p = fde_data; *p; ++p)
			{
				Dwarf_Fde f = *p;
				if (pc >= f->low_pc && pc < f->low_pc + f->func_length)
				{
					if (returned_fde) *returned_fde = f;
					if (lopc) *lopc = f->low_pc;
					if (hipc) *hipc = f->low_pc + f->func_length;
					return DW_DLV_OK;
				}
			}
			return DW_DLV_NO_ENTRY;
		}

		int dwarf_get_CFA_name(unsigned int val_in, const char **s_out)
		{
			switch (val_in & 0xc0)
			{
				case DW_CFA_advance_loc: *s_out = "DW_CFA_advance_loc"; return DW_DLV_OK;
				case DW_CFA_offset:      *s_out = "DW_CFA_offset";      return DW_DLV_OK;
				case DW_CFA_restore:     *s_out = "DW_CFA_restore";     return DW_DLV_OK;
				default: break;
			}
			switch (val_in)
			{
				case DW_CFA_nop:                *s_out = "DW_CFA_nop"; break;
				case DW_CFA_set_loc:            *s_out = "DW_CFA_set_loc"; break;
				case DW_CFA_advance_loc1:       *s_out = "DW_CFA_advance_loc1"; break;
				case DW_CFA_advance_loc2:       *s_out = "DW_CFA_advance_loc2"; break;
				case DW_CFA_advance_loc4:       *s_out = "DW_CFA_advance_loc4"; break;
				case DW_CFA_offset_extended:    *s_out = "DW_CFA_offset_extended"; break;
				case DW_CFA_restore_extended:   *s_out = "DW_CFA_restore_extended"; break;
				case DW_CFA_undefined:          *s_out = "DW_CFA_undefined"; break;
				case DW_CFA_same_value:         *s_out = "DW_CFA_same_value"; break;
				case DW_CFA_register:           *s_out = "DW_CFA_register"; break;
				case DW_CFA_remember_state:     *s_out = "DW_CFA_remember_state"; break;
				case DW_CFA_restore_state:      *s_out = "DW_CFA_restore_state"; break;
				case DW_CFA_def_cfa:            *s_out = "DW_CFA_def_cfa"; break;
				case DW_CFA_def_cfa_register:   *s_out = "DW_CFA_def_cfa_register"; break;
				case DW_CFA_def_cfa_offset:     *s_out = "DW_CFA_def_cfa_offset"; break;
				case DW_CFA_def_cfa_expression: *s_out = "DW_CFA_def_cfa_expression"; break;
				case DW_CFA_expression:         *s_out = "DW_CFA_expression"; break;
				case DW_CFA_offset_extended_sf: *s_out = "DW_CFA_offset_extended_sf"; break;
				case DW_CFA_def_cfa_sf:         *s_out = "DW_CFA_def_cfa_sf"; break;
				case DW_CFA_def_cfa_offset_sf:  *s_out = "DW_CFA_def_cfa_offset_sf"; break;
				case DW_CFA_val_offset:         *s_out = "DW_CFA_val_offset"; break;
				case DW_CFA_val_offset_sf:      *s_out = "DW_CFA_val_offset_sf"; break;
				case DW_CFA_val_expression:     *s_out = "DW_CFA_val_expression"; break;
				case DW_CFA_GNU_args_size:      *s_out = "DW_CFA_GNU_args_size"; break;
				case DW_CFA_GNU_window_save:    *s_out = "DW_CFA_GNU_window_save"; break;
				default:                        *s_out = "DW_CFA_unknown"; break;
			}
			return DW_DLV_OK;
		}

		/* dwarf_expand_frame_instructions is not needed: libdwarfpp decodes the
		 * instruction stream itself (encap::frame_instrlist). */
		int dwarf_expand_frame_instructions(Dwarf_Cie, Dwarf_Ptr, Dwarf_Unsigned,
			Dwarf_Frame_Op **, Dwarf_Signed *, Dwarf_Error *) { return DW_DLV_NO_ENTRY; }
		Dwarf_Half dwarf_set_frame_rule_table_size(Dwarf_Debug, Dwarf_Half value) { return value; }
		Dwarf_Half dwarf_set_frame_rule_initial_value(Dwarf_Debug, Dwarf_Half value) { return value; }
		Dwarf_Half dwarf_set_frame_cfa_value(Dwarf_Debug, Dwarf_Half value) { return value; }
		Dwarf_Half dwarf_set_frame_same_value(Dwarf_Debug, Dwarf_Half value) { return value; }
		Dwarf_Half dwarf_set_frame_undefined_value(Dwarf_Debug, Dwarf_Half value) { return value; }

	} // namespace lib
} // namespace dwarf
