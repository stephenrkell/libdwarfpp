/* dwarfpp: C++ binding for a useful subset of libdwarf/libdw, plus extra goodies.
 * 
 * dwarflib.hpp: a simple common abstraction over the underlying DWARF libraries
 *
 * Copyright (c) 2008--20, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_DWARFLIB_HPP_
#define DWARFPP_DWARFLIB_HPP_

#include <iostream>
#include <libelf.h>
#include "config.h" /* our configure-generated header, for HAVE_DWARF_FRAME_OP3 */

/* This file should be where we paper over differences between libdw and libdwarf,
 * aside from handle-related things, that go in dwarflib-handles.hpp. */

namespace dwarf
{
	namespace lib
	{
		//using namespace ::dwarf::spec;
		extern "C"
		{
#ifdef USING_LIBDWARF
			// HACK: libdwarf.h declares struct Elf opaquely, and we don't
			// want it in the dwarf::lib namespace, so preprocess this.
			#define Elf Elf_opaque_in_libdwarf
#endif
			#include "dwarf-lib.h"
#if USING_LIBDWARF
			#undef Elf
#endif
		}
		// forward decls
		struct loclist;

#if !HAVE_DWARF_SMALL
		typedef unsigned char      Dwarf_Small;  /* 1 byte unsigned value */
#endif
#ifdef USING_LIBDW
		typedef Dwarf_Word         Dwarf_Unsigned;
		typedef Dwarf_Sword        Dwarf_Signed;
		typedef int                Dwarf_Bool;
		typedef void*              Dwarf_Ptr;    /* host machine pointer */
#endif

		// we need this soon
		/* The point of this class is to make a self-contained throwable object
		 * out of a Dwarf_Error handle. FIXME: why do we bundle the Ptr but not the
		 * error function? */
#ifdef USING_LIBDW
		typedef void *Dwarf_Error;
#endif
		struct Error {
			Dwarf_Error e;
			Dwarf_Ptr arg;
			Error(Dwarf_Error e, Dwarf_Ptr arg) : e(e), arg(arg) {}
			virtual ~Error() 
			{ /*dwarf_dealloc((Dwarf_Debug) arg, e, DW_DLA_ERROR); */ /* TODO: Fix segfault here */	}
		};
		void default_error_handler(Dwarf_Error error, Dwarf_Ptr errarg);
		struct No_entry {
			No_entry() {}
		};

#if !HAVE_DWARF_RANGES
/* Pasted from libdwarf.h. FIXME: abstract this */
		enum Dwarf_Ranges_Entry_Type
		{
			DW_RANGES_ENTRY,
			DW_RANGES_ADDRESS_SELECTION,
			DW_RANGES_END
		};
		typedef struct {
			Dwarf_Addr dwr_addr1;
			Dwarf_Addr dwr_addr2;
			enum Dwarf_Ranges_Entry_Type  dwr_type;
		} Dwarf_Ranges;
#endif
		bool operator==(const Dwarf_Ranges& e1, const Dwarf_Ranges& e2);
		bool operator!=(const Dwarf_Ranges& e1, const Dwarf_Ranges& e2);
		std::ostream& operator<<(std::ostream& s, const Dwarf_Ranges& e);

#if !HAVE_DWARF_LOC
#if HAVE_DWARF_OP
		typedef Dwarf_Op Dwarf_Loc;
#define lr_atom atom
#define lr_number number
#define lr_number2 number2
#define lr_offset offset
#else
#error "Need Dwarf_Op (libdw) or Dwarf_Loc (libdwarf) to be defined"
#endif
#endif
		bool operator==(const Dwarf_Loc& e1, const Dwarf_Loc& e2);
		bool operator!=(const Dwarf_Loc& e1, const Dwarf_Loc& e2);
		bool operator<(const lib::Dwarf_Loc& arg1, const lib::Dwarf_Loc& arg2);

#if !HAVE_DWARF_LOCDESC // this includes libdw! i.e. we borrow 
/* Pasted from libdwarf.h -- FIXME: abstract this */
typedef struct {
	Dwarf_Addr      ld_lopc;
	Dwarf_Addr      ld_hipc;
	Dwarf_Half      ld_cents;
	Dwarf_Loc*      ld_s;
	Dwarf_Small     ld_from_loclist;
	Dwarf_Unsigned  ld_section_offset;
} Dwarf_Locdesc;
/* libdw takes a more cavalier approach to allocation than libdwarf:
 * it "interns" the decoded location expression, meaning it idempotently
 * decodes it into library-allocated memory, and puts a pointer into a
 * private searcxh tree so that it can find it again later. The memory
 * for the interned decoded copy is freed only when the whole file is
 * closed.
 * We can simply build a Dwarf_Locdesc as a wrapper around the decoded
 * array of Dwarf_Op records.
 */
#endif
		std::ostream& operator<<(std::ostream& s, const Dwarf_Loc /* a.k.a. expr_instr */ & e);
		std::ostream& operator<<(std::ostream& s, const Dwarf_Locdesc& ld);

#if HAVE_DWARF_FRAME_OP3
		/* Avoid introducing a new/distinct type unnecessarily. */
		typedef Dwarf_Frame_Op3 frame_op;
#else
		struct frame_op {
			Dwarf_Small fp_base_op;
			Dwarf_Small fp_extended_op;
			Dwarf_Half fp_register;
			Dwarf_Unsigned fp_offset_or_block_len;
			Dwarf_Small *fp_expr_block;
			Dwarf_Off fp_instr_offset;
		};
#endif

// Dwarf_Block has different field names in libdw...
#ifdef USING_LIBDW
#define bl_data data
#define bl_len  length
#endif

	} /* end namespace lib */
}

#endif
