/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 *
 * libdw.hpp: the types and C entry points backing libdwarfpp with elfutils'
 * libdw, used directly (no libdwarf-compatibility shim).
 *
 * When the build is configured with --with-dwarf-backend=libdw
 * (DWARFPP_USE_LIBDW), the handle layer (libdwarf-handles.hpp) stores libdw's
 * by-value ::Dwarf_Die / ::Dwarf_Attribute structs *inline* and drives them
 * with the allocation-free helpers declared here, whose implementation lives in
 * src/libdw.cpp. The earlier approach -- a libdwarf-look-alike shim that
 * heap-allocated and dwarf_dealloc'd a wrapper per DIE and per attribute -- is
 * gone; eliminating that per-node malloc/free churn is the performance win of
 * going direct.
 *
 * Copyright (c) 2008--26, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_LIBDW_HPP_
#define DWARFPP_LIBDW_HPP_

#include "config.h" /* our configure-generated header, for DWARFPP_USE_LIBDW */

#if not defined(DWARFPP_USE_LIBDW) || !DWARFPP_USE_LIBDW
#error "include/dwarfpp/libdw.hpp is for the libdw backend only"
#endif

#include <iostream>
#include <vector>  /* the cold-path entry points return into caller-owned vectors */
#include <cstdint> /* for uintptr_t in the libdw struct mirrors below */

/* We pull in only the standard DWARF constants (DW_TAG_*, DW_AT_*, DW_FORM_*,
 * DW_OP_*, DW_EH_PE_*, DW_CFA_* ...) and the Elf type here. We deliberately do
 * NOT include <elfutils/libdw.h> in this public header: it would introduce the
 * by-value ::Dwarf_Die etc. and global ::Dwarf_Addr/::Dwarf_Off/::Dwarf_Half
 * typedefs, which would clash with consumers that bring dwarf::lib::Dwarf_Addr
 * & co. into scope. The actual libdw types are needed only by src/libdw.cpp,
 * which includes <elfutils/libdw.h> itself and reinterpret_casts the mirrors
 * declared below to the real libdw structs. */
extern "C" {
#include <libelf.h>
#include <dwarf.h>
}

/* Constants libdwarf has but elfutils' <dwarf.h> lacks. */
#include "dwarfpp/libdw-compat-constants.h"

namespace dwarf
{
	namespace lib
	{
		/* ---- scalar types ------------------------------------------------
		 * We mirror David Anderson's libdwarf typedefs (same widths) so that
		 * the rest of libdwarfpp, which spells things dwarf::lib::Dwarf_Off
		 * etc., is unaffected by the choice of backend. */
		typedef unsigned long long Dwarf_Off;
		typedef unsigned long long Dwarf_Unsigned;
		typedef unsigned short     Dwarf_Half;
		typedef unsigned char      Dwarf_Small;
		typedef signed long long   Dwarf_Signed;
		typedef unsigned long long Dwarf_Addr;
		typedef int                Dwarf_Bool;
		typedef void*              Dwarf_Ptr;

		/* libdwarf renames Elf opaquely; with libdw we use the real Elf. */
		typedef ::Elf Elf_opaque_in_libdwarf;

		/* ---- handle types -----------------------------------------------
		 * The original libdwarf model hands out pointers to opaque structs and
		 * frees them with dwarf_dealloc(). libdw, by contrast, gives out small
		 * by-value structs (::Dwarf_Die, ::Dwarf_Attribute) that own nothing --
		 * they are lightweight references into the ::Dwarf's parsed data.
		 *
		 * The DIRECT backend exploits this: rather than heap-allocate a wrapper
		 * per DIE/attribute (as a libdwarf-compatible shim must), it stores the
		 * by-value struct *inline* inside the C++ handle object (dwarf::core::Die
		 * / Attribute). DIE-tree traversal and attribute iteration then allocate
		 * nothing per node, which is the whole point of going direct.
		 *
		 * Dwarf_Die_s / Dwarf_Attribute_s below are therefore TRANSPARENT, and
		 * laid out identically to elfutils' ::Dwarf_Die / ::Dwarf_Attribute. We
		 * deliberately do NOT include <elfutils/libdw.h> here: its global
		 * Dwarf_Off / Dwarf_Addr / Dwarf_Word typedefs would clash with the
		 * dwarf::lib ones below wherever a consumer does `using namespace
		 * dwarf::lib`. Instead src/libdw.cpp (the only file that includes the
		 * real header) reinterpret_casts between these mirrors and the libdw
		 * structs, guarded by static_asserts that the layouts match exactly.
		 *
		 * The pointer-shaped fields are spelled as uintptr_t rather than void*
		 * (libdwarfpp never dereferences them -- only src/libdw.cpp does, after
		 * casting to the real type). This keeps the layout identical while
		 * ensuring that a consumer whose own debug info happens to describe one
		 * of these structs (these handles live inside iterator_base, after all)
		 * sees plain integer members, not pointers-to-void -- so a type walk
		 * over such a consumer does not run off the end into the void type. */
		struct Dwarf_Debug_s;
		typedef Dwarf_Debug_s *Dwarf_Debug;
		struct Dwarf_Die_s
		{
			uintptr_t addr;
			uintptr_t cu;        /* really struct Dwarf_CU* */
			uintptr_t abbrev;    /* really Dwarf_Abbrev*    */
			long      padding__;
		};
		typedef Dwarf_Die_s *Dwarf_Die;
		struct Dwarf_Attribute_s
		{
			unsigned int code;
			unsigned int form;
			uintptr_t    valp;   /* really unsigned char*   */
			uintptr_t    cu;     /* really struct Dwarf_CU* */
		};
		typedef Dwarf_Attribute_s *Dwarf_Attribute;
		struct Dwarf_Error_s;
		typedef Dwarf_Error_s *Dwarf_Error;

		/* Frame info handles (stubbed backend, declared so frame.hpp compiles). */
		struct Dwarf_Cie_s;
		typedef Dwarf_Cie_s *Dwarf_Cie;
		struct Dwarf_Fde_s;
		typedef Dwarf_Fde_s *Dwarf_Fde;
		struct Dwarf_Line_s;
		typedef Dwarf_Line_s *Dwarf_Line;
		struct Dwarf_Arange_s;
		typedef Dwarf_Arange_s *Dwarf_Arange;
		struct Dwarf_Global_s;
		typedef Dwarf_Global_s *Dwarf_Global;

		typedef void (*Dwarf_Handler)(Dwarf_Error error, Dwarf_Ptr errarg);

		/* ---- transparent structs (layout-compatible with libdwarf) ------- */
		typedef struct {
			Dwarf_Unsigned bl_len;
			Dwarf_Ptr      bl_data;
			Dwarf_Small    bl_from_loclist;
			Dwarf_Unsigned bl_section_offset;
		} Dwarf_Block;

		typedef struct {
			Dwarf_Small    lr_atom;
			Dwarf_Unsigned lr_number;
			Dwarf_Unsigned lr_number2;
			Dwarf_Unsigned lr_offset;
		} Dwarf_Loc;

		typedef struct {
			Dwarf_Addr     ld_lopc;
			Dwarf_Addr     ld_hipc;
			Dwarf_Half     ld_cents;
			Dwarf_Loc     *ld_s;
			Dwarf_Small    ld_from_loclist;
			Dwarf_Unsigned ld_section_offset;
		} Dwarf_Locdesc;

		enum Dwarf_Ranges_Entry_Type {
			DW_RANGES_ENTRY,
			DW_RANGES_ADDRESS_SELECTION,
			DW_RANGES_END
		};
		typedef struct {
			Dwarf_Addr dwr_addr1;
			Dwarf_Addr dwr_addr2;
			enum Dwarf_Ranges_Entry_Type dwr_type;
		} Dwarf_Ranges;

		typedef struct {
			Dwarf_Small    fp_base_op;
			Dwarf_Small    fp_extended_op;
			Dwarf_Half     fp_register;
			Dwarf_Unsigned fp_offset_or_block_len;
			Dwarf_Small   *fp_expr_block;
			Dwarf_Off      fp_instr_offset;
		} Dwarf_Frame_Op;
		/* The DWARF3-style op is identical in shape for our purposes. */
		typedef Dwarf_Frame_Op Dwarf_Frame_Op3;
		typedef Dwarf_Frame_Op frame_op;

		/* ---- libdwarf result/allocation constants ------------------------ */
		#define DW_DLV_NO_ENTRY (-1)
		#define DW_DLV_OK         0
		#define DW_DLV_ERROR      1

		#define DW_DLC_READ       0

		#define DW_DLA_STRING     0x01
		#define DW_DLA_LOCDESC    0x03
		#define DW_DLA_BLOCK      0x06
		#define DW_DLA_DIE        0x08
		#define DW_DLA_LINE       0x09
		#define DW_DLA_ATTR       0x0a
		#define DW_DLA_GLOBAL     0x0d
		#define DW_DLA_ERROR      0x0e
		#define DW_DLA_LIST       0x0f
		#define DW_DLA_ARANGE     0x11
		#define DW_DLA_ABBREV     0x12
		#define DW_DLA_CIE        0x14
		#define DW_DLA_FDE        0x15
		#define DW_DLA_LOC_BLOCK  0x16
		#define DW_DLA_FUNC       0x18
		#define DW_DLA_TYPENAME   0x19
		#define DW_DLA_VAR        0x1a
		#define DW_DLA_WEAK       0x1b

		/* ---- the exception/error plumbing used across libdwarfpp ---------- */
		struct Error {
			Dwarf_Error e;
			Dwarf_Ptr arg;
			Error(Dwarf_Error e, Dwarf_Ptr arg) : e(e), arg(arg) {}
			~Error() { /* nothing to free: libdw owns its error state */ }
		};
		struct No_entry {
			No_entry() {}
		};
		void default_error_handler(Dwarf_Error error, Dwarf_Ptr errarg);

		/* operator overloads on the transparent structs (defined in
		 * libdwarf-data.cpp, which is shared between backends). */
		bool operator==(const Dwarf_Ranges& e1, const Dwarf_Ranges& e2);
		bool operator!=(const Dwarf_Ranges& e1, const Dwarf_Ranges& e2);
		std::ostream& operator<<(std::ostream& s, const Dwarf_Ranges& e);
		bool operator==(const Dwarf_Loc& e1, const Dwarf_Loc& e2);
		bool operator!=(const Dwarf_Loc& e1, const Dwarf_Loc& e2);
		bool operator<(const Dwarf_Loc& arg1, const Dwarf_Loc& arg2);
		std::ostream& operator<<(std::ostream& s, const Dwarf_Loc& e);
		std::ostream& operator<<(std::ostream& s, const Dwarf_Locdesc& ld);

		/* ================================================================== *
		 *  The C entry points onto libdw, implemented in src/libdw.cpp.        *
		 *  The hot path (traversal/attrs above) is allocation-free; the cold   *
		 *  path below (loclists, ranges, srcfiles, frame/CFI) still allocates  *
		 *  the small libdwarf-shaped result buffers it returns.                *
		 * ================================================================== */

		/* --- session / sections --- */
		int  dwarf_init(int fd, int access, Dwarf_Handler errhand, Dwarf_Ptr errarg,
			Dwarf_Debug *ret_dbg, Dwarf_Error *error);
		int  dwarf_elf_init(Elf_opaque_in_libdwarf *elf, int access,
			Dwarf_Handler errhand, Dwarf_Ptr errarg,
			Dwarf_Debug *ret_dbg, Dwarf_Error *error);
		int  dwarf_finish(Dwarf_Debug dbg, Dwarf_Error *error);
		int  dwarf_get_elf(Dwarf_Debug dbg, Elf_opaque_in_libdwarf **elf,
			Dwarf_Error *error);

		/* --- CU / DIE traversal ---
		 * The traversal primitives are allocation-free: the caller supplies an
		 * `out` pointing at its own inline Dwarf_Die_s storage, and the helper
		 * fills it in place (returning DW_DLV_OK / DW_DLV_NO_ENTRY / DW_DLV_ERROR).
		 * This is what lets the DIE-tree walk run without per-node malloc/free. */
		int  dwarf_next_cu_header_b(Dwarf_Debug dbg,
			Dwarf_Unsigned *cu_header_length, Dwarf_Half *version_stamp,
			Dwarf_Unsigned *abbrev_offset, Dwarf_Half *address_size,
			Dwarf_Half *offset_size, Dwarf_Half *extension_size,
			Dwarf_Unsigned *next_cu_header_offset, Dwarf_Error *error);
		int  dwarfpp_siblingof(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Die out);
		int  dwarfpp_child(Dwarf_Die die, Dwarf_Die out);
		int  dwarfpp_offdie(Dwarf_Debug dbg, Dwarf_Off off, Dwarf_Die out);
		int  dwarfpp_cu_die(Dwarf_Debug dbg, Dwarf_Die out); /* first DIE of current CU */

		/* --- DIE accessors --- */
		int  dwarf_dieoffset(Dwarf_Die die, Dwarf_Off *ret_off, Dwarf_Error *error);
		int  dwarf_tag(Dwarf_Die die, Dwarf_Half *ret_tag, Dwarf_Error *error);
		int  dwarf_diename(Dwarf_Die die, char **ret_name, Dwarf_Error *error);
		int  dwarf_hasattr(Dwarf_Die die, Dwarf_Half attr, Dwarf_Bool *ret_bool,
			Dwarf_Error *error);
		int  dwarf_CU_dieoffset_given_die(Dwarf_Die die, Dwarf_Off *ret_off,
			Dwarf_Error *error);

		/* --- attribute access ---
		 * Like the traversal helpers, these write into caller-owned inline
		 * Dwarf_Attribute_s storage. dwarfpp_getattrs invokes `cb` once per
		 * attribute with a pointer to a temporary Dwarf_Attribute_s (the handle
		 * layer copies the small value into its vector), so a whole attribute
		 * list costs one vector allocation rather than one malloc per attr. */
		int  dwarfpp_attr(Dwarf_Die die, Dwarf_Half attr, Dwarf_Attribute out);
		int  dwarfpp_getattrs(Dwarf_Die die,
			int (*cb)(Dwarf_Attribute, void *), void *arg);
		int  dwarf_whatattr(Dwarf_Attribute attr, Dwarf_Half *ret_attr,
			Dwarf_Error *error);
		int  dwarf_whatform(Dwarf_Attribute attr, Dwarf_Half *ret_form,
			Dwarf_Error *error);

		/* --- form value extraction --- */
		int  dwarf_formstring(Dwarf_Attribute attr, char **ret, Dwarf_Error *error);
		int  dwarf_formflag(Dwarf_Attribute attr, Dwarf_Bool *ret, Dwarf_Error *error);
		int  dwarf_formaddr(Dwarf_Attribute attr, Dwarf_Addr *ret, Dwarf_Error *error);
		int  dwarf_formudata(Dwarf_Attribute attr, Dwarf_Unsigned *ret, Dwarf_Error *error);
		int  dwarf_formsdata(Dwarf_Attribute attr, Dwarf_Signed *ret, Dwarf_Error *error);
		int  dwarf_global_formref(Dwarf_Attribute attr, Dwarf_Off *ret, Dwarf_Error *error);
		/* Block is returned by value into caller-owned inline storage: the bytes
		 * belong to libdw, so there is nothing to free. */
		int  dwarfpp_formblock(Dwarf_Attribute attr, Dwarf_Block *out);

		/* --- deallocation / errors --- */
		void dwarf_dealloc(Dwarf_Debug dbg, void *space, Dwarf_Unsigned type);
		const char *dwarf_errmsg(Dwarf_Error error);
		int  dwarf_errno(Dwarf_Error error);

		/* ---- location lists, ranges, source files ------------------------ *
		 * Loclists are returned into caller-owned vectors: a true location list as
		 * one LoclistEntry per range-guarded descriptor (the handle layer then
		 * materialises each as an inline Dwarf_Locdesc), and a bare expression as
		 * a single op vector with its [lopc,hipc) guard. No malloc'd Dwarf_Locdesc
		 * to dwarf_dealloc. */
		struct LoclistEntry {
			std::vector<Dwarf_Loc> ops;
			Dwarf_Addr lopc, hipc;
		};
		int  dwarfpp_loclist(Dwarf_Attribute attr, std::vector<LoclistEntry>& out);
		int  dwarfpp_loclist_from_expr(Dwarf_Ptr bytes_in, Dwarf_Unsigned bytes_len,
			std::vector<Dwarf_Loc>& out_ops, Dwarf_Addr *out_lopc, Dwarf_Addr *out_hipc);
		int  dwarf_formexprloc(Dwarf_Attribute attr, Dwarf_Unsigned *ret_exprlen,
			Dwarf_Ptr *block_ptr, Dwarf_Error *error);
		/* Ranges and srcfiles are materialised straight into a caller-owned
		 * container (the handle's vector), so there is no malloc'd buffer to
		 * free. Ranges are keyed on the owning DIE; srcfile strings belong to
		 * libdw. Both return DW_DLV_{OK,NO_ENTRY,ERROR}. */
		int  dwarfpp_get_ranges(Dwarf_Die die, std::vector<Dwarf_Ranges>& out);
		int  dwarfpp_srcfiles(Dwarf_Die die, std::vector<const char*>& out);

		/* ---- staged: frame / CFI (stubbed) ------------------------------- */
		int  dwarf_get_fde_list(Dwarf_Debug dbg, Dwarf_Cie **cie_data,
			Dwarf_Signed *cie_element_count, Dwarf_Fde **fde_data,
			Dwarf_Signed *fde_element_count, Dwarf_Error *error);
		int  dwarf_get_fde_list_eh(Dwarf_Debug dbg, Dwarf_Cie **cie_data,
			Dwarf_Signed *cie_element_count, Dwarf_Fde **fde_data,
			Dwarf_Signed *fde_element_count, Dwarf_Error *error);
		int  dwarf_get_CFA_name(unsigned int val_in, const char **s_out);
		void dwarf_fde_cie_list_dealloc(Dwarf_Debug dbg, Dwarf_Cie *cie_data,
			Dwarf_Signed cie_element_count, Dwarf_Fde *fde_data,
			Dwarf_Signed fde_element_count);
		int  dwarf_get_fde_range(Dwarf_Fde fde, Dwarf_Addr *low_pc,
			Dwarf_Unsigned *func_length, Dwarf_Ptr *fde_bytes,
			Dwarf_Unsigned *fde_byte_length, Dwarf_Off *cie_offset,
			Dwarf_Signed *cie_index, Dwarf_Off *fde_offset, Dwarf_Error *error);
		int  dwarf_get_cie_of_fde(Dwarf_Fde fde, Dwarf_Cie *cie_returned,
			Dwarf_Error *error);
		int  dwarf_get_cie_info(Dwarf_Cie cie, Dwarf_Unsigned *bytes_in_cie,
			Dwarf_Small *version, char **augmenter,
			Dwarf_Unsigned *code_alignment_factor,
			Dwarf_Signed *data_alignment_factor,
			Dwarf_Half *return_address_register_rule,
			Dwarf_Ptr *initial_instructions,
			Dwarf_Unsigned *initial_instructions_length, Dwarf_Error *error);
		int  dwarf_get_cie_index(Dwarf_Cie cie, Dwarf_Signed *index,
			Dwarf_Error *error);
		int  dwarf_get_fde_instr_bytes(Dwarf_Fde fde, Dwarf_Ptr *outinstrs,
			Dwarf_Unsigned *outlen, Dwarf_Error *error);
		int  dwarf_get_fde_at_pc(Dwarf_Fde *fde_data, Dwarf_Addr pc_of_interest,
			Dwarf_Fde *returned_fde, Dwarf_Addr *lopc, Dwarf_Addr *hipc,
			Dwarf_Error *error);
		int  dwarf_expand_frame_instructions(Dwarf_Cie cie, Dwarf_Ptr instruction,
			Dwarf_Unsigned i_length, Dwarf_Frame_Op **returned_op_list,
			Dwarf_Signed *op_count, Dwarf_Error *error);
		Dwarf_Half dwarf_set_frame_rule_table_size(Dwarf_Debug dbg, Dwarf_Half value);
		Dwarf_Half dwarf_set_frame_rule_initial_value(Dwarf_Debug dbg, Dwarf_Half value);
		Dwarf_Half dwarf_set_frame_cfa_value(Dwarf_Debug dbg, Dwarf_Half value);
		Dwarf_Half dwarf_set_frame_same_value(Dwarf_Debug dbg, Dwarf_Half value);
		Dwarf_Half dwarf_set_frame_undefined_value(Dwarf_Debug dbg, Dwarf_Half value);

	} // namespace lib
} // namespace dwarf

#endif /* DWARFPP_LIBDW_HPP_ */
