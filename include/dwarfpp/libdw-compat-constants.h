/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 *
 * libdw-compat-constants.h: DWARF-related constants that exist in David
 * Anderson's libdwarf headers but are absent from elfutils' <dwarf.h>.
 * Provided so that libdwarfpp's source compiles unchanged under the libdw
 * backend. Guarded individually in case a future elfutils does define them.
 *
 * Copyright (c) 2008--26, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_LIBDW_COMPAT_CONSTANTS_H_
#define DWARFPP_LIBDW_COMPAT_CONSTANTS_H_

/* Withdrawn from DWARF3 by DWARF3f, but still referenced by spec tables. */
#ifndef DW_TAG_mutable_type
#define DW_TAG_mutable_type 0x3e
#endif

/* libdwarf frame-table pseudo-register column for the CFA (DWARF3 ABI). */
#ifndef DW_FRAME_CFA_COL3
#define DW_FRAME_CFA_COL3 1436
#endif

/* A libdwarf internal error code ("mangled debugging entry"). */
#ifndef DW_DLE_MDE
#define DW_DLE_MDE 10
#endif

#endif /* DWARFPP_LIBDW_COMPAT_CONSTANTS_H_ */
