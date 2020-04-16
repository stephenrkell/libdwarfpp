/* This is a simple program which uses libdw
 * to dump the constants defined by libdw,
 * as simple C preprocessor macros.
 * It's reflective: it must be compiled with debugging information.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cassert>
extern "C" {
#include "dwarf.h" /* from elfutils */
#include "libdw.h"
}
#include <err.h>

/* This program works reflectively, so ensure its own debug info
 * contains the types behind the libdw-defined constants. */
static auto access __attribute__((used)) = DW_ACCESS_public;
static auto attribute __attribute__((used)) = DW_AT_sibling;
static auto encoding __attribute__((used)) = DW_ATE_void;
static auto calling __attribute__((used)) = DW_CC_normal;
static auto cfa __attribute__((used)) = DW_CFA_nop;
static auto children __attribute__((used)) = DW_CHILDREN_no;
static auto cie_id __attribute__((used)) = DW_CIE_ID_32;
static auto defaulted __attribute__((used)) = DW_DEFAULTED_no;
static auto ds __attribute__((used)) = DW_DS_unsigned;
static auto dsc __attribute__((used)) = DW_DSC_label;
static auto eh_pe __attribute__((used)) = DW_EH_PE_absptr;
static auto end __attribute__((used)) = DW_END_default;
static auto form __attribute__((used)) = DW_FORM_addr;
static auto id __attribute__((used)) = DW_ID_case_sensitive;
static auto inl __attribute__((used)) = DW_INL_not_inlined;
static auto lang __attribute__((used)) = DW_LANG_C89;
static auto lle __attribute__((used)) = DW_LLE_end_of_list;
static auto lnct __attribute__((used)) = DW_LNCT_path;
static auto lne __attribute__((used)) = DW_LNE_end_sequence;
static auto lns __attribute__((used)) = DW_LNS_copy;
static auto macinfo __attribute__((used)) = DW_MACINFO_define;
static auto macro __attribute__((used)) = DW_MACRO_define;
static auto op __attribute__((used)) = DW_OP_addr;
static auto ord __attribute__((used)) = DW_ORD_row_major;
static auto rle __attribute__((used)) = DW_RLE_end_of_list;
static auto tag __attribute__((used)) = DW_TAG_array_type;
static auto ut __attribute__((used)) = DW_UT_compile;
static auto virtuality __attribute__((unused)) = DW_VIRTUALITY_none;
static auto vis __attribute__((unused)) = DW_VIS_local;

/* Use a verbose name to avoid conflicts with the identifier 'debug' */
static _Bool do_debug_output;
#define debug_print(...) do { if (do_debug_output) { fprintf(stderr, __VA_ARGS__); } } while (0)

static void do_visit(Dwarf *debug, Dwarf_Die *pos)
{
	Dwarf_Off off = dwarf_dieoffset(pos);
	debug_print("Saw a DIE at 0x%lx!\n", (unsigned long) off);
	if (dwarf_tag(pos) == DW_TAG_enumerator)
	{
		debug_print("Saw an enumerator DIE at 0x%lx!\n", (unsigned long) off);
		if (dwarf_hasattr(pos, DW_AT_name))
		{
			debug_print("It has a name!\n");
			Dwarf_Attribute a;
			Dwarf_Attribute *ret = dwarf_attr(pos, DW_AT_name, &a);
			assert(ret == &a);
			if (a.form == DW_FORM_strp)
			{
				debug_print("name is an indirect string!\n");
				const char *string = dwarf_formstring(&a);
				debug_print("name is %s!\n", string);
				if (strlen(string) >= 4 && 0 == strncmp(string, "DW_", 3))
				{
					debug_print("Looks like a DWARF constant!\n");
					if (dwarf_hasattr(pos, DW_AT_const_value))
					{
						Dwarf_Attribute value_a;
						Dwarf_Attribute *ret = dwarf_attr(pos, DW_AT_const_value, &value_a);
						assert(ret == &value_a);
						__int128 value = 0;
						switch (value_a.form) // block, constant, string
						{
							case DW_FORM_block1:
							case DW_FORM_block2:
							case DW_FORM_block4:
							case DW_FORM_block: {
								Dwarf_Block b;
								int ret = dwarf_formblock(&value_a, &b);
								assert(ret == 0);
								/* We've got a block, but we want an unsigned integer. */
								//memcpy(
								//value
							} break;
							case DW_FORM_data1:
								//value = *(signed char *) value_a.valp;
								//break;
							case DW_FORM_data2:
								//value = *(signed short *) value_a.valp;
								//break;
							case DW_FORM_data4:
								//value = *(signed int *) value_a.valp;
								//break;
							case DW_FORM_data8:
								//value = *(signed long *) value_a.valp;
								//break;
							case DW_FORM_data16:
								//value = *(signed __int128 *) value_a.valp;
								// break;
								/* These should be signed values, because DWARF enumerators
								 * are all signed. FIXME: add a check on the type. */
								
							case DW_FORM_sdata:
							case DW_FORM_udata: {
								Dwarf_Word val;
								int ret;
								if (0 != (ret = dwarf_formudata(&value_a, &val))) err(1, "reading data");
								value = (long) val;
							} break;
							case DW_FORM_implicit_const:
								break;

							default:
							case DW_FORM_string:
							case DW_FORM_strp:
							case DW_FORM_strp_sup:
								assert(0); // shouldn't be of string class
						}
						debug_print("Got a value? 0x%lx\n", (long) value);
						printf("#define %-32s 0x%lx\n", string, (long) value);
					}
				}
			}
		}
	}
}
static void walk_df_pre(Dwarf *debug, Dwarf_Die *pos)
{
	// it's a pre-order, so visit now
	do_visit(debug, pos);
	if (dwarf_haschildren(pos))
	{
		Dwarf_Die current; // initially will be the first child
		int ret = dwarf_child(pos, &current);
		assert(ret == 0);
		do // then iterate through siblings of that child
		{
			walk_df_pre(debug, &current);
		} while (0 == (ret = dwarf_siblingof(&current, &current)));
	}
}

int main(int argc, char **argv)
{
	if (getenv("DUMP_ENUMS_DEBUG") && !!atoi(getenv("DUMP_ENUMS_DEBUG"))) do_debug_output = 1;
	/* For any enumerator whose name begins with "DW_",
	 * we print its value. */
	FILE *f = fopen(argv[0], "r");
	if (!f) { err(1, "opening own executable file"); }
	//Dwarf_Debug d;
	Dwarf *d;
	//Dwarf_Error e;
	d = /*dwarf_init*/dwarf_begin(fileno(f), /*DW_DLC_READ*/ DWARF_C_READ); //, NULL,
		// NULL, &d, &e);
	//dwarf_finish(d, &e);
	assert(d);

	/* OK. We have the handle. Now iterate over CUs. */
// 	Dwarf_Off o = 0;
// 	Dwarf_Off next_o;
// 	size_t header_size;
// 	Dwarf_Off abbrev_offset;
// 	uint8_t address_size;
// 	uint8_t offset_size;
	//while (0 == (ret = dwarf_nextcu(d, o, &next_o, &header_size, &abbrev_offset,
	//	&address_size, &offset_size)))

	/* get the .debug_str section */

	Dwarf_CU *cur_cu = NULL;
	Dwarf_Die cudie = { 0 };
	Dwarf_Die subdie = { 0 };
	Dwarf_Half version;
	uint8_t unit_type;
	int ret = 0;
	while (0 == (ret = dwarf_get_units(d, cur_cu, &cur_cu, &version,
		&unit_type, &cudie, &subdie)))
	{
		/* We have the CU DIE. What is its offset? */
		Dwarf_Off cu_off = dwarf_dieoffset(&cudie);
		debug_print("Saw a CU at 0x%lx!\n", (unsigned long) cu_off);
		//o = next_o;

		//uint64_t unit_id; // what's this? Does it matter that we don't get it
		Dwarf_Off abbrev_offset;
		uint8_t address_size;
		uint8_t offset_size;
		Dwarf_Off type_offset;
		uint64_t type_signature;
		// ret = dwarf_cu_info(cu, &version, &unit_type, NULL, NULL,
		//     &unit_id, &address_size, &offset_size);
		Dwarf_Die tmp;
		if (NULL == dwarf_cu_die(cur_cu,
				&tmp, // we've already got the DIE
				&version, // we've already got the version too
				&abbrev_offset,
				&address_size,
				&offset_size,
				&type_signature,
				&type_offset)) { errx(1, "getting CU info"); }
		assert(0 == memcmp(&tmp, &cudie, sizeof tmp));

		/* Easiest way to depthfirst-traverse? Use recursion. */
		walk_df_pre(d, &cudie);
	}
	
	
	dwarf_end(d);
	return 0;
}
