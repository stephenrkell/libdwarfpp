#include <stdio.h>
// #include "dwarf.h" /* ideally from elfutils */ <-- get this via -include from the build system
#include "known-dwarf.h"
#include "libdw.h"

int main(void)
{
#define print_one(identlit, identval) \
	printf("#define " identlit " 0x%lx\n", (unsigned long) identval);

#define DWARF_ONE_KNOWN_DW_ACCESS(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_ACCESS
#define DWARF_ONE_KNOWN_DW_AT(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_AT
#define DWARF_ONE_KNOWN_DW_ATE(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_ATE
#define DWARF_ONE_KNOWN_DW_CC(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_CC
#define DWARF_ONE_KNOWN_DW_CFA(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_CFA
#define DWARF_ONE_KNOWN_DW_CHILDREN(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_CHILDREN
#define DWARF_ONE_KNOWN_DW_CIE_ID(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_CIE_ID
#define DWARF_ONE_KNOWN_DW_DEFAULTED(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_DEFAULTED
#define DWARF_ONE_KNOWN_DW_DS(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_DS
#define DWARF_ONE_KNOWN_DW_DSC(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_DSC
#define DWARF_ONE_KNOWN_DW_EH_PE(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_EH_PE
#define DWARF_ONE_KNOWN_DW_END(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_END
#define DWARF_ONE_KNOWN_DW_FORM(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_FORM
#define DWARF_ONE_KNOWN_DW_ID(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_ID
#define DWARF_ONE_KNOWN_DW_INL(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_INL
#define DWARF_ONE_KNOWN_DW_LANG(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_LANG
#define DWARF_ONE_KNOWN_DW_LLE(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_LLE
#define DWARF_ONE_KNOWN_DW_LLE_GNU(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_LLE_GNU
#define DWARF_ONE_KNOWN_DW_LNCT(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_LNCT
#define DWARF_ONE_KNOWN_DW_LNE(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_LNE
#define DWARF_ONE_KNOWN_DW_LNS(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_LNS
#define DWARF_ONE_KNOWN_DW_MACINFO(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_MACINFO
#define DWARF_ONE_KNOWN_DW_MACRO(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_MACRO
#define DWARF_ONE_KNOWN_DW_OP(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_OP
#define DWARF_ONE_KNOWN_DW_ORD(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_ORD
#define DWARF_ONE_KNOWN_DW_RLE(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_RLE
// HACK: some builds of libdw are broken
#ifndef LIBDW_KNOWN_SECT_INFO_BROKEN
#define DWARF_ONE_KNOWN_DW_SECT_INFO(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_SECT_INFO
#endif
#define DWARF_ONE_KNOWN_DW_TAG(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_TAG
#define DWARF_ONE_KNOWN_DW_UT(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_UT
#define DWARF_ONE_KNOWN_DW_VIRTUALITY(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_VIRTUALITY
#define DWARF_ONE_KNOWN_DW_VIS(shortf, ident) print_one(#ident, ident)
	DWARF_ALL_KNOWN_DW_VIS

	return 0;
}
