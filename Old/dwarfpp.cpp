/* Minimal C++ binding for a useful subset of libdwarf.
 * 
 * Copyright (c) 2008, Stephen Kell.
 */

#include <cassert>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <limits>
#include <algorithm>
#include "dwarfpp.hpp"

namespace dwarf {

// generic list-making macros

#define PAIR_ENTRY_FORWARDS(sym) (std::make_pair(#sym, (sym))),
#define PAIR_ENTRY_BACKWARDS(sym) (std::make_pair((sym), #sym)),
#define PAIR_ENTRY_FORWARDS_LAST(sym) (std::make_pair(#sym, (sym)))
#define PAIR_ENTRY_BACKWARDS_LAST(sym) (std::make_pair((sym), #sym))

#define PAIR_ENTRY_FORWARDS_VARARGS(sym, ...) (std::make_pair(#sym, (sym))),
#define PAIR_ENTRY_BACKWARDS_VARARGS(sym, ...) (std::make_pair((sym), #sym)),
#define PAIR_ENTRY_FORWARDS_VARARGS_LAST(sym, ...) (std::make_pair(#sym, (sym)))
#define PAIR_ENTRY_BACKWARDS_VARARGS_LAST(sym, ...) (std::make_pair((sym), #sym))

#define MAKE_LOOKUP(pair_type, name, make_pair, last_pair, list) \
pair_type name[] = { \
						list(make_pair, last_pair) \
					}

// tag-specific list definitions
#define TAG_PAIR_LIST(make_pair, last_pair) \
						make_pair(DW_TAG_array_type) \
						make_pair(DW_TAG_class_type) \
						make_pair(DW_TAG_entry_point) \
						make_pair(DW_TAG_enumeration_type) \
						make_pair(DW_TAG_formal_parameter) \
						make_pair(DW_TAG_imported_declaration) \
						make_pair(DW_TAG_label) \
						make_pair(DW_TAG_lexical_block) \
						make_pair(DW_TAG_member) \
						make_pair(DW_TAG_pointer_type) \
						make_pair(DW_TAG_reference_type) \
						make_pair(DW_TAG_compile_unit) \
						make_pair(DW_TAG_string_type) \
						make_pair(DW_TAG_structure_type) \
						make_pair(DW_TAG_subroutine_type) \
						make_pair(DW_TAG_typedef) \
						make_pair(DW_TAG_union_type) \
						make_pair(DW_TAG_unspecified_parameters) \
						make_pair(DW_TAG_variant) \
						make_pair(DW_TAG_common_block) \
						make_pair(DW_TAG_common_inclusion) \
						make_pair(DW_TAG_inheritance) \
						make_pair(DW_TAG_inlined_subroutine) \
						make_pair(DW_TAG_module) \
						make_pair(DW_TAG_ptr_to_member_type) \
						make_pair(DW_TAG_set_type) \
						make_pair(DW_TAG_subrange_type) \
						make_pair(DW_TAG_with_stmt) \
						make_pair(DW_TAG_access_declaration) \
						make_pair(DW_TAG_base_type) \
						make_pair(DW_TAG_catch_block) \
						make_pair(DW_TAG_const_type) \
						make_pair(DW_TAG_constant) \
						make_pair(DW_TAG_enumerator) \
						make_pair(DW_TAG_file_type) \
						make_pair(DW_TAG_friend) \
						make_pair(DW_TAG_namelist) \
						make_pair(DW_TAG_namelist_item) /* DWARF3/2 spelling */ \
						make_pair(DW_TAG_packed_type) \
						make_pair(DW_TAG_subprogram) \
						make_pair(DW_TAG_template_type_parameter) /* DWARF3/2 spelling*/ \
						make_pair(DW_TAG_template_value_parameter) /* DWARF3/2 spelling*/ \
						make_pair(DW_TAG_thrown_type) \
						make_pair(DW_TAG_try_block) \
						make_pair(DW_TAG_variant_part) \
						make_pair(DW_TAG_variable) \
						make_pair(DW_TAG_volatile_type) \
						make_pair(DW_TAG_dwarf_procedure) /* DWARF3 */ \
						make_pair(DW_TAG_restrict_type) /* DWARF3 */ \
						make_pair(DW_TAG_interface_type) /* DWARF3 */ \
						make_pair(DW_TAG_namespace) /* DWARF3 */ \
						make_pair(DW_TAG_imported_module) /* DWARF3 */ \
						make_pair(DW_TAG_unspecified_type) /* DWARF3 */ \
						make_pair(DW_TAG_partial_unit) /* DWARF3 */ \
						make_pair(DW_TAG_imported_unit) /* DWARF3 */ \
						make_pair(DW_TAG_mutable_type) /* Withdrawn from DWARF3 by DWARF3f. */ \
						make_pair(DW_TAG_condition) /* DWARF3f */ \
						last_pair(DW_TAG_shared_type) /* DWARF3f */

	bool tag_is_type(Dwarf_Half tag)
	{
		return tag == DW_TAG_array_type
			|| tag == DW_TAG_class_type
			|| tag == DW_TAG_enumeration_type
			|| tag == DW_TAG_imported_declaration
			|| tag == DW_TAG_pointer_type
			|| tag == DW_TAG_reference_type
			|| tag == DW_TAG_string_type
			|| tag == DW_TAG_structure_type
			|| tag == DW_TAG_subroutine_type
			|| tag == DW_TAG_typedef
			|| tag == DW_TAG_union_type
			|| tag == DW_TAG_variant
			|| tag == DW_TAG_ptr_to_member_type
			|| tag == DW_TAG_set_type
			|| tag == DW_TAG_subrange_type
			|| tag == DW_TAG_base_type
			|| tag == DW_TAG_const_type
			|| tag == DW_TAG_file_type
			|| tag == DW_TAG_friend
			|| tag == DW_TAG_packed_type
			|| tag == DW_TAG_volatile_type
			|| tag == DW_TAG_restrict_type
			|| tag == DW_TAG_interface_type
			|| tag == DW_TAG_unspecified_type
			|| tag == DW_TAG_mutable_type
			|| tag == DW_TAG_shared_type;
	}
	
	/* This predicate is intended to match DWARF entries on which we might do
	 * Cake's "name : pred"-style checking/assertions, where the 'name'd elements
	 * are *immediate* child DIEs -- *not* DIEs referenced by offset in attributes. */
	bool tag_has_named_children(Dwarf_Half tag)
	{
		return tag == DW_TAG_class_type
			|| tag == DW_TAG_enumeration_type
			|| tag == DW_TAG_lexical_block // really?
			|| tag == DW_TAG_compile_unit
			|| tag == DW_TAG_structure_type
			|| tag == DW_TAG_subroutine_type
			|| tag == DW_TAG_union_type
			|| tag == DW_TAG_variant
			|| tag == DW_TAG_common_block // really?
			|| tag == DW_TAG_inheritance
			|| tag == DW_TAG_module
			|| tag == DW_TAG_set_type
			|| tag == DW_TAG_catch_block // really?
			|| tag == DW_TAG_namelist
			|| tag == DW_TAG_subprogram
			|| tag == DW_TAG_try_block // really?
			|| tag == DW_TAG_interface_type
			|| tag == DW_TAG_namespace
			|| tag == DW_TAG_imported_module
			|| tag == DW_TAG_partial_unit
			|| tag == DW_TAG_imported_unit;	
	}			
						
	typedef std::pair<const char *, Dwarf_Half> tag_forward_mapping_t;
	typedef std::pair<Dwarf_Half, const char *> tag_inverse_mapping_t;
	MAKE_LOOKUP(tag_forward_mapping_t, tag_forward_tbl, PAIR_ENTRY_FORWARDS, PAIR_ENTRY_FORWARDS_LAST, TAG_PAIR_LIST);
	MAKE_LOOKUP(tag_inverse_mapping_t, tag_inverse_tbl, PAIR_ENTRY_BACKWARDS, PAIR_ENTRY_BACKWARDS_LAST, TAG_PAIR_LIST);
	std::map<const char *, Dwarf_Half> tag_forward_map(&tag_forward_tbl[0],
		&tag_forward_tbl[sizeof tag_forward_tbl / sizeof (tag_forward_mapping_t) - 1]);
	std::map<Dwarf_Half, const char *> tag_inverse_map(&tag_inverse_tbl[0],
		&tag_inverse_tbl[sizeof tag_inverse_tbl / sizeof (tag_inverse_mapping_t) - 1]);

	const char *tag_lookup(Dwarf_Half tag) 
	{
		std::map<Dwarf_Half, const char *>::iterator found = tag_inverse_map.find(tag);
		if (found != tag_inverse_map.end()) return found->second;
		else return "(unknown tag)";
	}
		
#define ATTR_DECL_LIST(make_decl, last_decl) \
						make_decl(DW_AT_sibling, interp::reference ) \
						make_decl(DW_AT_location, interp::block, interp::loclistptr ) \
						make_decl(DW_AT_name, interp::string ) \
						make_decl(DW_AT_ordering, interp::constant ) \
						make_decl(DW_AT_byte_size, interp::block, interp::constant, interp::reference ) \
						make_decl(DW_AT_bit_offset, interp::block, interp::constant, interp::reference ) \
						make_decl(DW_AT_bit_size, interp::block, interp::constant, interp::reference ) \
						make_decl(DW_AT_stmt_list, interp::lineptr ) \
						make_decl(DW_AT_low_pc, interp::address ) \
						make_decl(DW_AT_high_pc, interp::address ) \
						make_decl(DW_AT_language, interp::constant ) \
						make_decl(DW_AT_discr, interp::reference ) \
						make_decl(DW_AT_discr_value, interp::constant ) \
						make_decl(DW_AT_visibility, interp::constant ) \
						make_decl(DW_AT_import, interp::reference ) \
						make_decl(DW_AT_string_length, interp::block, interp::loclistptr ) \
						make_decl(DW_AT_common_reference, interp::reference ) \
						make_decl(DW_AT_comp_dir, interp::string ) \
						make_decl(DW_AT_const_value, interp::block, interp::constant, interp::string ) \
						make_decl(DW_AT_containing_type, interp::reference ) \
						make_decl(DW_AT_default_value, interp::reference ) \
						make_decl(DW_AT_inline, interp::constant ) \
						make_decl(DW_AT_is_optional, interp::flag ) \
						make_decl(DW_AT_lower_bound, interp::block, interp::constant, interp::reference ) \
						make_decl(DW_AT_producer, interp::string ) \
						make_decl(DW_AT_prototyped, interp::flag ) \
						make_decl(DW_AT_return_addr, interp::block, interp::loclistptr ) \
						make_decl(DW_AT_start_scope, interp::constant ) \
						make_decl(DW_AT_bit_stride, interp::constant ) \
						make_decl(DW_AT_upper_bound, interp::block, interp::constant, interp::reference ) \
						make_decl(DW_AT_abstract_origin, interp::reference ) \
						make_decl(DW_AT_accessibility, interp::constant ) \
						make_decl(DW_AT_address_class, interp::constant ) \
						make_decl(DW_AT_artificial, interp::flag ) \
						make_decl(DW_AT_base_types, interp::reference ) \
						make_decl(DW_AT_calling_convention, interp::constant ) \
						make_decl(DW_AT_count, interp::block, interp::constant, interp::reference ) \
						make_decl(DW_AT_data_member_location, interp::block, interp::constant, interp::loclistptr ) \
						make_decl(DW_AT_decl_column, interp::constant ) \
						make_decl(DW_AT_decl_file, interp::constant ) \
						make_decl(DW_AT_decl_line, interp::constant ) \
						make_decl(DW_AT_declaration, interp::flag ) \
						make_decl(DW_AT_discr_list, interp::block ) \
						make_decl(DW_AT_encoding, interp::constant ) \
						make_decl(DW_AT_external, interp::flag ) \
						make_decl(DW_AT_frame_base, interp::block, interp::loclistptr ) \
						make_decl(DW_AT_friend, interp::reference ) \
						make_decl(DW_AT_identifier_case, interp::constant ) \
						make_decl(DW_AT_macro_info, interp::macptr ) \
						make_decl(DW_AT_namelist_item, interp::block ) \
						make_decl(DW_AT_priority, interp::reference ) \
						make_decl(DW_AT_segment, interp::block, interp::loclistptr ) \
						make_decl(DW_AT_specification, interp::reference ) \
						make_decl(DW_AT_static_link, interp::block, interp::loclistptr ) \
						make_decl(DW_AT_type, interp::reference ) \
						make_decl(DW_AT_use_location, interp::block, interp::loclistptr ) \
						make_decl(DW_AT_variable_parameter, interp::flag ) \
						make_decl(DW_AT_virtuality, interp::constant ) \
						make_decl(DW_AT_vtable_elem_location, interp::block, interp::loclistptr ) \
						make_decl(DW_AT_allocated, interp::block, interp::constant, interp::reference ) \
						make_decl(DW_AT_associated, interp::block, interp::constant, interp::reference ) \
						make_decl(DW_AT_data_location, interp::block ) \
						make_decl(DW_AT_byte_stride, interp::block, interp::constant, interp::reference ) \
						make_decl(DW_AT_entry_pc, interp::address ) \
						make_decl(DW_AT_use_UTF8, interp::flag ) \
						make_decl(DW_AT_extension, interp::reference ) \
						make_decl(DW_AT_ranges, interp::rangelistptr ) \
						make_decl(DW_AT_trampoline, interp::address, interp::flag, interp::reference, interp::string ) \
						make_decl(DW_AT_call_column, interp::constant ) \
						make_decl(DW_AT_call_file, interp::constant ) \
						make_decl(DW_AT_call_line, interp::constant ) \
						make_decl(DW_AT_description, interp::string ) \
						make_decl(DW_AT_binary_scale, interp::constant ) \
						make_decl(DW_AT_decimal_scale, interp::constant ) \
						make_decl(DW_AT_small, interp::reference ) \
						make_decl(DW_AT_decimal_sign, interp::constant ) \
						make_decl(DW_AT_digit_count, interp::constant ) \
						make_decl(DW_AT_picture_string, interp::string ) \
						make_decl(DW_AT_mutable, interp::flag ) \
						make_decl(DW_AT_threads_scaled, interp::flag ) \
						make_decl(DW_AT_explicit, interp::flag ) \
						make_decl(DW_AT_object_pointer, interp::reference ) \
						make_decl(DW_AT_endianity, interp::constant ) \
						make_decl(DW_AT_elemental, interp::flag ) \
						last_decl(DW_AT_pure, interp::flag )

	bool attr_describes_location(Dwarf_Half attr)
	{
		return attr == DW_AT_location
			|| attr == DW_AT_data_member_location
			|| attr == DW_AT_vtable_elem_location
			|| attr == DW_AT_string_length
			|| attr == DW_AT_use_location
			|| attr == DW_AT_return_addr;
	}
						
	typedef std::pair<const char *, Dwarf_Half> attr_forward_mapping_t;
	typedef std::pair<Dwarf_Half, const char *> attr_inverse_mapping_t;
	MAKE_LOOKUP(attr_forward_mapping_t, attr_forward_tbl, PAIR_ENTRY_FORWARDS_VARARGS, PAIR_ENTRY_FORWARDS_VARARGS_LAST, ATTR_DECL_LIST);
	MAKE_LOOKUP(attr_inverse_mapping_t, attr_inverse_tbl, PAIR_ENTRY_BACKWARDS_VARARGS, PAIR_ENTRY_BACKWARDS_VARARGS_LAST, ATTR_DECL_LIST);
	std::map<const char *, Dwarf_Half> attr_forward_map(&attr_forward_tbl[0],
		&attr_forward_tbl[sizeof attr_forward_tbl / sizeof (attr_forward_mapping_t) - 1]);
	std::map<Dwarf_Half, const char *> attr_inverse_map(&attr_inverse_tbl[0],
		&attr_inverse_tbl[sizeof attr_inverse_tbl / sizeof (attr_inverse_mapping_t) - 1]);

	// define an array of interpretation classes for each DW_AT_ constant
	#define make_array_declaration(attr_name, ...) interp::interp_class attr_classes_ ## attr_name[] = { __VA_ARGS__, interp::EOL }; //
	ATTR_DECL_LIST(make_array_declaration, make_array_declaration)

	// define a lookup table from DW_AT_ constants to the arrays just created
	#define make_associative_entry(attr_name, ...) std::make_pair(attr_name, attr_classes_ ## attr_name),
	#define last_associative_entry(attr_name, ...) std::make_pair(attr_name, attr_classes_ ## attr_name),
	typedef std::pair<Dwarf_Half, interp::interp_class *> attr_class_mapping_t;	
	MAKE_LOOKUP(attr_class_mapping_t, attr_class_tbl_array, make_associative_entry, last_associative_entry, ATTR_DECL_LIST);
	std::map<Dwarf_Half, interp::interp_class *> attr_class_tbl(&attr_class_tbl_array[0],
		&attr_class_tbl_array[sizeof attr_class_tbl_array / sizeof (attr_class_mapping_t) - 1]);

	const char *attr_lookup(Dwarf_Half attr) 
	{
		std::map<Dwarf_Half, const char *>::iterator found = attr_inverse_map.find(attr);
		if (found != attr_inverse_map.end()) return found->second;
		else return "(unknown attribute)";
	}

#define FORM_DECL_LIST(make_decl, last_decl) \
						make_decl(DW_FORM_addr, interp::address)  \
						make_decl(DW_FORM_block2, interp::block)  \
						make_decl(DW_FORM_block4, interp::block)  \
						make_decl(DW_FORM_data2, interp::constant)  \
						make_decl(DW_FORM_data4, interp::constant, interp::lineptr, interp::loclistptr, interp::macptr, interp::rangelistptr)  \
						make_decl(DW_FORM_data8, interp::constant, interp::lineptr, interp::loclistptr, interp::macptr, interp::rangelistptr)  \
						make_decl(DW_FORM_string, interp::string)  \
						make_decl(DW_FORM_block, interp::block)  \
						make_decl(DW_FORM_block1, interp::block)  \
						make_decl(DW_FORM_data1, interp::constant)  \
						make_decl(DW_FORM_flag, interp::flag)  \
						make_decl(DW_FORM_sdata, interp::constant)  \
						make_decl(DW_FORM_strp, interp::string)  \
						make_decl(DW_FORM_udata, interp::constant)  \
						make_decl(DW_FORM_ref_addr, interp::reference)  \
						make_decl(DW_FORM_ref1, interp::reference)  \
						make_decl(DW_FORM_ref2, interp::reference)  \
						make_decl(DW_FORM_ref4, interp::reference)  \
						make_decl(DW_FORM_ref8, interp::reference)  \
						make_decl(DW_FORM_ref_udata, interp::reference)  \
						last_decl(DW_FORM_indirect, interp::EOL) 

	typedef std::pair<const char *, Dwarf_Half> form_forward_mapping_t;
	typedef std::pair<Dwarf_Half, const char *> form_inverse_mapping_t;
	MAKE_LOOKUP(form_forward_mapping_t, form_forward_tbl, PAIR_ENTRY_FORWARDS_VARARGS, PAIR_ENTRY_FORWARDS_VARARGS_LAST, FORM_DECL_LIST);
	MAKE_LOOKUP(form_inverse_mapping_t, form_inverse_tbl, PAIR_ENTRY_BACKWARDS_VARARGS, PAIR_ENTRY_BACKWARDS_VARARGS_LAST, FORM_DECL_LIST);
	std::map<const char *, Dwarf_Half> form_forward_map(&form_forward_tbl[0],
		&form_forward_tbl[sizeof form_forward_tbl / sizeof (form_forward_mapping_t) - 1]);
	std::map<Dwarf_Half, const char *> form_inverse_map(&form_inverse_tbl[0],
		&form_inverse_tbl[sizeof form_inverse_tbl / sizeof (form_inverse_mapping_t) - 1]);

	// define an array of interpretation classes for each DW_FORM_ constant
	#undef make_array_declaration
	#define make_array_declaration(form_name, ...) interp::interp_class form_classes_ ## form_name[] = { __VA_ARGS__, interp::EOL }; //
	FORM_DECL_LIST(make_array_declaration, make_array_declaration)

	// define a lookup table from DW_FORM_ constants to the arrays just created
	#undef make_associative_entry
	#undef last_associative_entry
	#define make_associative_entry(form_name, ...) std::make_pair(form_name, form_classes_ ## form_name),
	#define last_associative_entry(form_name, ...) std::make_pair(form_name, form_classes_ ## form_name),
	typedef std::pair<Dwarf_Half, interp::interp_class *> form_class_mapping_t;	
	MAKE_LOOKUP(form_class_mapping_t, form_class_tbl_array, make_associative_entry, last_associative_entry, FORM_DECL_LIST);
	std::map<Dwarf_Half, interp::interp_class *> form_class_tbl(&form_class_tbl_array[0],
		&form_class_tbl_array[sizeof form_class_tbl_array / sizeof (form_class_mapping_t) - 1]);

	const char *form_lookup(Dwarf_Half form) 
	{
		std::map<Dwarf_Half, const char *>::iterator found = form_inverse_map.find(form);
		if (found != form_inverse_map.end()) return found->second;
		else return "(unknown form)";
	}

#define ENCODING_PAIR_LIST(make_pair, last_pair) \
						make_pair(DW_ATE_address)  \
						make_pair(DW_ATE_boolean)  \
						make_pair(DW_ATE_complex_float)  \
						make_pair(DW_ATE_float)  \
						make_pair(DW_ATE_signed)  \
						make_pair(DW_ATE_signed_char)  \
						make_pair(DW_ATE_unsigned)  \
						make_pair(DW_ATE_unsigned_char)  \
						make_pair(DW_ATE_imaginary_float)  \
						make_pair(DW_ATE_packed_decimal)  \
						make_pair(DW_ATE_numeric_string)  \
						make_pair(DW_ATE_edited)  \
						make_pair(DW_ATE_signed_fixed)  \
						make_pair(DW_ATE_unsigned_fixed)  \
						last_pair(DW_ATE_decimal_float) 
					
	typedef std::pair<const char *, Dwarf_Half> encoding_forward_mapping_t;
	typedef std::pair<Dwarf_Half, const char *> encoding_inverse_mapping_t;
	MAKE_LOOKUP(encoding_forward_mapping_t, encoding_forward_tbl, PAIR_ENTRY_FORWARDS, PAIR_ENTRY_FORWARDS_LAST, ENCODING_PAIR_LIST);
	MAKE_LOOKUP(encoding_inverse_mapping_t, encoding_inverse_tbl, PAIR_ENTRY_BACKWARDS, PAIR_ENTRY_BACKWARDS_LAST, ENCODING_PAIR_LIST);
	std::map<const char *, Dwarf_Half> encoding_forward_map(&encoding_forward_tbl[0],
		&encoding_forward_tbl[sizeof encoding_forward_tbl / sizeof (encoding_forward_mapping_t) - 1]);
	std::map<Dwarf_Half, const char *> encoding_inverse_map(&encoding_inverse_tbl[0],
		&encoding_inverse_tbl[sizeof encoding_inverse_tbl / sizeof (encoding_inverse_mapping_t) - 1]);

	const char *encoding_lookup(Dwarf_Half encoding) 
	{
		std::map<Dwarf_Half, const char *>::iterator found = encoding_inverse_map.find(encoding);
		if (found != encoding_inverse_map.end()) return found->second;
		else return "(unknown encoding)";
	}

#define OP_DECL_LIST(make_decl, last_decl) \
					make_decl(DW_OP_addr, DW_FORM_addr) \
					make_decl(DW_OP_deref, 0) \
					make_decl(DW_OP_const1u, DW_FORM_data1) \
					make_decl(DW_OP_const1s, DW_FORM_data1) \
					make_decl(DW_OP_const2u, DW_FORM_data2) \
					make_decl(DW_OP_const2s, DW_FORM_data2) \
					make_decl(DW_OP_const4u, DW_FORM_data4) \
					make_decl(DW_OP_const4s, DW_FORM_data4) \
					make_decl(DW_OP_const8u, DW_FORM_data8) \
					make_decl(DW_OP_const8s, DW_FORM_data8) \
					make_decl(DW_OP_constu, DW_FORM_udata) \
					make_decl(DW_OP_consts, DW_FORM_sdata) \
					make_decl(DW_OP_dup, 0) \
					make_decl(DW_OP_drop, 0) \
					make_decl(DW_OP_over, 0) \
					make_decl(DW_OP_pick, DW_FORM_data1) \
					make_decl(DW_OP_swap, 0) \
					make_decl(DW_OP_rot, 0) \
					make_decl(DW_OP_xderef, 0) \
					make_decl(DW_OP_abs, 0) \
					make_decl(DW_OP_and, 0) \
					make_decl(DW_OP_div, 0) \
					make_decl(DW_OP_minus, 0) \
					make_decl(DW_OP_mod, 0) \
					make_decl(DW_OP_mul, 0) \
					make_decl(DW_OP_neg, 0) \
					make_decl(DW_OP_not, 0) \
					make_decl(DW_OP_or, 0) \
					make_decl(DW_OP_plus, 0) \
					make_decl(DW_OP_plus_uconst, DW_FORM_udata) \
					make_decl(DW_OP_shl, 0) \
					make_decl(DW_OP_shr, 0) \
					make_decl(DW_OP_shra, 0) \
					make_decl(DW_OP_xor, 0) \
					make_decl(DW_OP_bra, DW_FORM_data2) \
					make_decl(DW_OP_eq, 0) \
					make_decl(DW_OP_ge, 0) \
					make_decl(DW_OP_gt, 0) \
					make_decl(DW_OP_le, 0) \
					make_decl(DW_OP_lt, 0) \
					make_decl(DW_OP_ne, 0) \
					make_decl(DW_OP_skip, DW_FORM_data2) \
					make_decl(DW_OP_lit0, 0) \
					make_decl(DW_OP_lit1, 0) \
					make_decl(DW_OP_lit2, 0) \
					make_decl(DW_OP_lit3, 0) \
					make_decl(DW_OP_lit4, 0) \
					make_decl(DW_OP_lit5, 0) \
					make_decl(DW_OP_lit6, 0) \
					make_decl(DW_OP_lit7, 0) \
					make_decl(DW_OP_lit8, 0) \
					make_decl(DW_OP_lit9, 0) \
					make_decl(DW_OP_lit10, 0) \
					make_decl(DW_OP_lit11, 0) \
					make_decl(DW_OP_lit12, 0) \
					make_decl(DW_OP_lit13, 0) \
					make_decl(DW_OP_lit14, 0) \
					make_decl(DW_OP_lit15, 0) \
					make_decl(DW_OP_lit16, 0) \
					make_decl(DW_OP_lit17, 0) \
					make_decl(DW_OP_lit18, 0) \
					make_decl(DW_OP_lit19, 0) \
					make_decl(DW_OP_lit20, 0) \
					make_decl(DW_OP_lit21, 0) \
					make_decl(DW_OP_lit22, 0) \
					make_decl(DW_OP_lit23, 0) \
					make_decl(DW_OP_lit24, 0) \
					make_decl(DW_OP_lit25, 0) \
					make_decl(DW_OP_lit26, 0) \
					make_decl(DW_OP_lit27, 0) \
					make_decl(DW_OP_lit28, 0) \
					make_decl(DW_OP_lit29, 0) \
					make_decl(DW_OP_lit30, 0) \
					make_decl(DW_OP_lit31, 0) \
					make_decl(DW_OP_reg0, 0) \
					make_decl(DW_OP_reg1, 0) \
					make_decl(DW_OP_reg2, 0) \
					make_decl(DW_OP_reg3, 0) \
					make_decl(DW_OP_reg4, 0) \
					make_decl(DW_OP_reg5, 0) \
					make_decl(DW_OP_reg6, 0) \
					make_decl(DW_OP_reg7, 0) \
					make_decl(DW_OP_reg8, 0) \
					make_decl(DW_OP_reg9, 0) \
					make_decl(DW_OP_reg10, 0) \
					make_decl(DW_OP_reg11, 0) \
					make_decl(DW_OP_reg12, 0) \
					make_decl(DW_OP_reg13, 0) \
					make_decl(DW_OP_reg14, 0) \
					make_decl(DW_OP_reg15, 0) \
					make_decl(DW_OP_reg16, 0) \
					make_decl(DW_OP_reg17, 0) \
					make_decl(DW_OP_reg18, 0) \
					make_decl(DW_OP_reg19, 0) \
					make_decl(DW_OP_reg20, 0) \
					make_decl(DW_OP_reg21, 0) \
					make_decl(DW_OP_reg22, 0) \
					make_decl(DW_OP_reg23, 0) \
					make_decl(DW_OP_reg24, 0) \
					make_decl(DW_OP_reg25, 0) \
					make_decl(DW_OP_reg26, 0) \
					make_decl(DW_OP_reg27, 0) \
					make_decl(DW_OP_reg28, 0) \
					make_decl(DW_OP_reg29, 0) \
					make_decl(DW_OP_reg30, 0) \
					make_decl(DW_OP_reg31, 0) \
					make_decl(DW_OP_breg0, DW_FORM_sdata) \
					make_decl(DW_OP_breg1, DW_FORM_sdata) \
					make_decl(DW_OP_breg2, DW_FORM_sdata) \
					make_decl(DW_OP_breg3, DW_FORM_sdata) \
					make_decl(DW_OP_breg4, DW_FORM_sdata) \
					make_decl(DW_OP_breg5, DW_FORM_sdata) \
					make_decl(DW_OP_breg6, DW_FORM_sdata) \
					make_decl(DW_OP_breg7, DW_FORM_sdata) \
					make_decl(DW_OP_breg8, DW_FORM_sdata) \
					make_decl(DW_OP_breg9, DW_FORM_sdata) \
					make_decl(DW_OP_breg10, DW_FORM_sdata) \
					make_decl(DW_OP_breg11, DW_FORM_sdata) \
					make_decl(DW_OP_breg12, DW_FORM_sdata) \
					make_decl(DW_OP_breg13, DW_FORM_sdata) \
					make_decl(DW_OP_breg14, DW_FORM_sdata) \
					make_decl(DW_OP_breg15, DW_FORM_sdata) \
					make_decl(DW_OP_breg16, DW_FORM_sdata) \
					make_decl(DW_OP_breg17, DW_FORM_sdata) \
					make_decl(DW_OP_breg18, DW_FORM_sdata) \
					make_decl(DW_OP_breg19, DW_FORM_sdata) \
					make_decl(DW_OP_breg20, DW_FORM_sdata) \
					make_decl(DW_OP_breg21, DW_FORM_sdata) \
					make_decl(DW_OP_breg22, DW_FORM_sdata) \
					make_decl(DW_OP_breg23, DW_FORM_sdata) \
					make_decl(DW_OP_breg24, DW_FORM_sdata) \
					make_decl(DW_OP_breg25, DW_FORM_sdata) \
					make_decl(DW_OP_breg26, DW_FORM_sdata) \
					make_decl(DW_OP_breg27, DW_FORM_sdata) \
					make_decl(DW_OP_breg28, DW_FORM_sdata) \
					make_decl(DW_OP_breg29, DW_FORM_sdata) \
					make_decl(DW_OP_breg30, DW_FORM_sdata) \
					make_decl(DW_OP_breg31, DW_FORM_sdata) \
					make_decl(DW_OP_regx, 0) \
					make_decl(DW_OP_fbreg, 0) \
					make_decl(DW_OP_bregx, DW_FORM_udata, DW_FORM_sdata) \
					make_decl(DW_OP_piece, DW_FORM_udata) \
					make_decl(DW_OP_deref_size, DW_FORM_data1) \
					make_decl(DW_OP_xderef_size, DW_FORM_data1) \
					make_decl(DW_OP_nop, 0) \
					make_decl(DW_OP_push_object_address, 0) \
					make_decl(DW_OP_call2, DW_FORM_data2) \
					make_decl(DW_OP_call4, DW_FORM_data4) \
					make_decl(DW_OP_call_ref, DW_FORM_ref_udata /* FIXME */) \
					make_decl(DW_OP_form_tls_address, 0) \
					make_decl(DW_OP_call_frame_cfa, 0) \
					make_decl(DW_OP_bit_piece, 0) \
					make_decl(DW_OP_GNU_push_tls_address, 0) \
					make_decl(DW_OP_HP_is_value, 0) \
					make_decl(DW_OP_HP_fltconst4, 0) \
					make_decl(DW_OP_HP_fltconst8, 0) \
					make_decl(DW_OP_HP_mod_range, 0) \
					make_decl(DW_OP_HP_unmod_range, 0) \
					make_decl(DW_OP_HP_tls, 0) \
					last_decl(DW_OP_hi_user, 0)
					
	typedef std::pair<const char *, Dwarf_Half> op_forward_mapping_t;
	typedef std::pair<Dwarf_Half, const char *> op_inverse_mapping_t;
	MAKE_LOOKUP(op_forward_mapping_t, op_forward_tbl, PAIR_ENTRY_FORWARDS_VARARGS, PAIR_ENTRY_FORWARDS_VARARGS_LAST, OP_DECL_LIST);
	MAKE_LOOKUP(op_inverse_mapping_t, op_inverse_tbl, PAIR_ENTRY_BACKWARDS_VARARGS, PAIR_ENTRY_BACKWARDS_VARARGS_LAST, OP_DECL_LIST);
	std::map<const char *, Dwarf_Half> op_forward_map(&op_forward_tbl[0],
		&op_forward_tbl[sizeof op_forward_tbl / sizeof (op_forward_mapping_t) - 1]);
	std::map<Dwarf_Half, const char *> op_inverse_map(&op_inverse_tbl[0],
		&op_inverse_tbl[sizeof op_inverse_tbl / sizeof (op_inverse_mapping_t) - 1]);

	// define an array of interpretation classes for each DW_FORM_ constant
	#undef make_array_declaration
	#define make_array_declaration(op_name, ...) Dwarf_Half op_operand_forms_ ## op_name[] = { __VA_ARGS__, 0 }; //
	OP_DECL_LIST(make_array_declaration, make_array_declaration)

	// define a lookup table from DW_OP_ constants to the arrays just created
	#undef make_associative_entry
	#undef last_associative_entry
	#define make_associative_entry(op_name, ...) std::make_pair(op_name, op_operand_forms_ ## op_name),
	#define last_associative_entry(op_name, ...) std::make_pair(op_name, op_operand_forms_ ## op_name),
	typedef std::pair<Dwarf_Half, Dwarf_Half *> op_operand_forms_mapping_t;	
	MAKE_LOOKUP(op_operand_forms_mapping_t, op_operand_forms_tbl_array, make_associative_entry, last_associative_entry, OP_DECL_LIST);
	std::map<Dwarf_Half, Dwarf_Half *> op_operand_forms_tbl(&op_operand_forms_tbl_array[0],
		&op_operand_forms_tbl_array[sizeof op_operand_forms_tbl_array / sizeof (op_operand_forms_mapping_t) - 1]);

	const char *op_lookup(Dwarf_Half op) 
	{
		std::map<Dwarf_Half, const char *>::iterator found = op_inverse_map.find(op);
		if (found != op_inverse_map.end()) return found->second;
		else return "(unknown op)";
	}
	
	Dwarf_Unsigned op_operand_count(Dwarf_Half op) 
	{
		int count = 0;
		// linear count-up
		for (Dwarf_Half *p_form = op_operand_forms_tbl[op]; *p_form != 0; p_form++) count++;
		return count;
	}

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
		Dwarf_Error *error /*= 0*/)
	{
    	if (error == 0) error = &last_error;
		if (errarg == 0) errarg = this;
		int retval;
		retval = dwarf_init(fd, access, errhand, errarg, &dbg, error);
		// the default error handler will always throw an exception
		assert(retval == DW_DLV_OK || errhand != default_error_handler);
	}

	file::~file()
	{
		//Elf *elf; // maybe this Elf business is only for dwarf_elf_open?
		//int retval;
		//retval = dwarf_get_elf(dbg, &elf, &last_error);
		dwarf_finish(dbg, &last_error);
		//elf_end(elf);
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
		return dwarf_next_cu_header(dbg, cu_header_length, version_stamp,
        		abbrev_offset, address_size, next_cu_header, error); // may allocate **error
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
		return dwarf_child(my_die, &(return_kid->my_die), error);
	} // may allocate **error, allocates *(return_kid->my_die) on return

	int die::tag(
		Dwarf_Half *tagval,
		Dwarf_Error *error /*= 0*/)
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
		char ** return_name,
		Dwarf_Error *error /*= 0*/)
	{
		if (error == 0) error = p_last_error;
		int retval = dwarf_diename(my_die, return_name, error);
		//std::cerr << "Retval from dwarf_diename is " << retval << std::endl;
		return retval;
	} // may allocate **name, else may allocate **error
	/* TODO: here we want to return an object whose destructor calls dwarf_dealloc
	 * on the allocated string. Should we subclass std::string? or just copy the
	 * characters and return by value? or define dwarf::string with an implicit
	 * conversion to char* ?
 	 */

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
	
	/*
	 * methods defined on attribute
	 */
	int attribute::hasform(Dwarf_Half form, Dwarf_Bool *return_hasform,
		Dwarf_Error *error /*=0*/)
	{
		if (error == 0) error = a.p_last_error;
		return dwarf_hasform(a.p_attrs[i], form, return_hasform, error);
	}
	int attribute::whatform(Dwarf_Half *return_form, Dwarf_Error *error /*=0*/)
	{
		if (error == 0) error = a.p_last_error;
		return dwarf_whatform(a.p_attrs[i], return_form, error);
	}
	int attribute::whatform_direct(Dwarf_Half *return_form,	Dwarf_Error *error /*=0*/)
	{
		if (error == 0) error = a.p_last_error;
		return dwarf_whatform_direct(a.p_attrs[i], return_form, error);
	}
	int attribute::whatattr(Dwarf_Half *return_attr, Dwarf_Error *error /*=0*/)
	{		
		if (error == 0) error = a.p_last_error;
		return dwarf_whatattr(a.p_attrs[i], return_attr, error);
	}
	int attribute::formref(Dwarf_Off *return_offset, Dwarf_Error *error /*=0*/)
	{		
		if (error == 0) error = a.p_last_error;
		return dwarf_formref(a.p_attrs[i], return_offset, error);
	}
	int attribute::formref_global(Dwarf_Off *return_offset, Dwarf_Error *error /*=0*/)
	{		
		if (error == 0) error = a.p_last_error;
		return dwarf_global_formref(a.p_attrs[i], return_offset, error);
	}
	int attribute::formaddr(Dwarf_Addr * return_addr, Dwarf_Error *error /*=0*/)
	{		
		if (error == 0) error = a.p_last_error;
		return dwarf_formaddr(a.p_attrs[i], return_addr, error);
	}
	int attribute::formflag(Dwarf_Bool * return_bool, Dwarf_Error *error /*=0*/)
	{		
		if (error == 0) error = a.p_last_error;
		return dwarf_formflag(a.p_attrs[i], return_bool, error);
	}
	int attribute::formudata(Dwarf_Unsigned * return_uvalue, Dwarf_Error * error /*=0*/)
	{		
		if (error == 0) error = a.p_last_error;
		return dwarf_formudata(a.p_attrs[i], return_uvalue, error);
	}
	int attribute::formsdata(Dwarf_Signed * return_svalue, Dwarf_Error *error /*=0*/)
	{		
		if (error == 0) error = a.p_last_error;
		return dwarf_formsdata(a.p_attrs[i], return_svalue, error);
	}
	int attribute::formblock(Dwarf_Block ** return_block, Dwarf_Error * error /*=0*/)
	{		
		if (error == 0) error = a.p_last_error; // TODO: fix this to be RAII
		return dwarf_formblock(a.p_attrs[i], return_block, error);
	}
	int attribute::formstring(char ** return_string, Dwarf_Error *error /*=0*/)
	{		
		if (error == 0) error = a.p_last_error; // TODO: fix this to be RAII
		return dwarf_formstring(a.p_attrs[i], return_string, error);
	}
	int attribute::loclist_n(Dwarf_Locdesc ***llbuf, Dwarf_Signed *listlen, Dwarf_Error *error /*=0*/)
	{
		if (error == 0) error = a.p_last_error; // TODO: fix this to be RAII
		return dwarf_loclist_n(a.p_attrs[i], llbuf, listlen, error);
	}
	int attribute::loclist(Dwarf_Locdesc **llbuf, Dwarf_Signed *listlen, Dwarf_Error *error /*=0*/)
	{
		if (error == 0) error = a.p_last_error; // TODO: fix this to be RAII
		return dwarf_loclist(a.p_attrs[i], llbuf, listlen, error);
	}
}
std::ostream& operator<<(std::ostream& s, const Dwarf_Locdesc& ld)
{
	s << dwarf::encap::expr(ld);
	return s;
}	
std::ostream& operator<<(std::ostream& s, const Dwarf_Loc& l)
{
	s << "0x" << std::hex << l.lr_offset << std::dec
		<< ": " << dwarf::op_lookup(l.lr_atom);
	std::ostringstream buf;
	std::string to_append;
	switch (dwarf::op_operand_count(l.lr_atom))
	{
		case 2:
			buf << ", " << dwarf::encap::attribute_value(
				l.lr_number2, dwarf::op_operand_forms_tbl[l.lr_atom][1]);
			to_append += buf.str();
		case 1:
			buf.clear();
			buf << "(" << dwarf::encap::attribute_value(
				l.lr_number, dwarf::op_operand_forms_tbl[l.lr_atom][0]);
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
const dwarf::encap::attribute_value::form dwarf::encap::attribute_value::dwarf_form_to_form(const Dwarf_Half form)
{
	switch (form)
	{
		case DW_FORM_addr:
			return dwarf::encap::attribute_value::ADDR;
		case DW_FORM_block2:
		case DW_FORM_block4:
		case DW_FORM_data2:
		case DW_FORM_data4:
		case DW_FORM_data8:
		case DW_FORM_block:
		case DW_FORM_block1:
		case DW_FORM_data1:
		case DW_FORM_udata:
			return dwarf::encap::attribute_value::UNSIGNED;
		case DW_FORM_string:
		case DW_FORM_strp:
			return dwarf::encap::attribute_value::STRING;
		case DW_FORM_sdata:
			return dwarf::encap::attribute_value::SIGNED;
		case DW_FORM_flag:
		case DW_FORM_ref_addr:
		case DW_FORM_ref1:
		case DW_FORM_ref2:
		case DW_FORM_ref4:
		case DW_FORM_ref8:
		case DW_FORM_ref_udata:
		case DW_FORM_indirect:
		default:
			assert(false);
			return dwarf::encap::attribute_value::NO_ATTR;
	}	
}
namespace dwarf {	
	std::ostream& operator<<(std::ostream& s, const ::dwarf::loclist& ll)
	{
		s << "(loclist) {";
		for (int i = 0; i < ll.len(); i++)
		{
			if (i > 0) s << ", ";
			s << ll[i];
		}		
		return s;
	}
	namespace encap {
		/*
		 * encap definitions
		 */
		std::ostream& operator<<(std::ostream& s, const ::dwarf::encap::loclist& ll)
		{
			s << "(loclist) {";
			for (::dwarf::encap::loclist::const_iterator i = ll.begin(); i != ll.end(); i++)
			{
				if (i != ll.begin()) s << ", ";
				s << *i;
			}
			s << "}";
			return s;
		}
		std::ostream& operator<<(std::ostream& s, const expr& e)
		{
			s << "loc described by { ";
			for (std::vector<Dwarf_Loc>::const_iterator i = e.m_expr.begin(); i != e.m_expr.end(); i++)
			{
				s << *i;
			}
			s << " } (for ";
			if (e.lopc == 0 && e.hipc == std::numeric_limits<Dwarf_Addr>::max()) // FIXME
			{
				s << "all vaddrs";
			}
			else
			{
				s << "vaddr 0x"
					<< std::hex << e.lopc << std::dec
					<< "..0x"
					<< std::hex << e.hipc << std::dec;
			}
			s << ")";
			return s;
		}		
		void attribute_value::print_raw(std::ostream& s) const
		{
			switch (f)
			{
				case NO_ATTR:
					s << "(not present)";
					break;
				case FLAG:
					s << "(flag) " << (v_flag ? "true" : "false");
					break;				
				case UNSIGNED:
					s << "(unsigned) " << v_u;
					break;				
				case SIGNED:
					s << "(signed) " << v_s;
					break;
				case BLOCK:
					s << "(block) ";
					for (std::vector<unsigned char>::iterator p = v_block->begin(); p != v_block->end(); p++)
					{
						//s.setf(std::ios::hex);
						s << std::hex << (int) *p << std::dec << " ";
						//s.unsetf(std::ios::hex);
					}
					break;
				case STRING:
					s << "(string) " << *v_string;
					break;
				
				case REF:
					s << "(reference, " << (v_ref->abs ? "global) " : "nonglobal) ");
					s << "0x" << std::hex << v_ref->off << std::dec;
					
					break;
				
				case ADDR:
					s << "(address) 0x" << std::hex << v_addr << std::dec;
					break;
				
				case LOCLIST:
					print_as(s, interp::loclistptr);
					break;
				
				default: 
					s << "FIXME! (not present)";
					break;
			
			}			
		} // end attribute_value::print	

		interp::interp_class get_interp(const Dwarf_Half attr, const Dwarf_Half form)
		{
			return get_extended_interp(attr, form);
		}

		interp::interp_class get_extended_interp(const Dwarf_Half attr, const Dwarf_Half form)
		{
			interp::interp_class cls = get_basic_interp(attr, form);
			switch(cls)
			{
				case interp::block:
					if (attr_describes_location(attr))
					{
						return interp::block_as_dwarf_expr;
					}
					// else fall through
				default:
					return cls;
			}
		}
		
		interp::interp_class get_basic_interp(const Dwarf_Half attr, const Dwarf_Half form)
		{
			interp::interp_class *attr_possible_classes = attr_class_tbl[attr];
			interp::interp_class *form_possible_classes = form_class_tbl[form];
			std::vector<interp::interp_class> possibles;
			if (attr_possible_classes == 0
				&& form_possible_classes == 0) goto fail;
			// these lists are both terminated by interp::EOL
			
			/* Fudge: to avoid bombing out when encountering future DWARF standards 
			 * or vendor extensions, we want to be able to handle unknown attributes
			 * or unknown forms as far as possible. Therefore, we should tolerate
			 * cases where either attr_possible_classes *or* form_possible_classes
			 * are empty (but not both), *and* the nonempty one is a singleton. The
			 * singleton effectively determines that at best, one interpretation is
			 * possible, so we return it. It may be erroneous to do so; at worst this 
			 * just means we store a bogus (but hopefully not unsafe) interpretation of 
			 * attributes we don't understand. */
			 
			// by this point, we know the lists may be *either* null *or* non-null but empty
			if ((attr_possible_classes == 0 || attr_possible_classes[0] == interp::EOL)
			&& (form_possible_classes != 0 && form_possible_classes[0] != interp::EOL))
			{
				if (form_possible_classes[1] == interp::EOL) //return form_possible_classes[0];
					possibles.push_back(form_possible_classes[0]);
			}
			else if ((attr_possible_classes != 0 && attr_possible_classes[0] != interp::EOL)
			&& (form_possible_classes == 0 || form_possible_classes[0] == interp::EOL))
			{
				if (attr_possible_classes[1] == interp::EOL) //return attr_possible_classes[0];
					possibles.push_back(attr_possible_classes[0]);
			}
			else
			{
				// if we hit this block, either neither list is empty, or both are
							
				// find the intersection of these two lists
				for (interp::interp_class *p_attr_cls = attr_possible_classes;
					p_attr_cls != 0 && *p_attr_cls != interp::EOL; ++p_attr_cls)
				{
					// just return the first possible interpretation
					for (interp::interp_class *p_form_cls = form_possible_classes;
						p_form_cls != 0 && *p_form_cls != interp::EOL; ++p_form_cls)
					{
						if (*p_form_cls == *p_attr_cls) possibles.push_back(*p_form_cls);
					}
				}
			}
			
			switch (possibles.size())
			{
				case 1: // this is the good case
					return *(possibles.begin());
				fail:
				case 0:
					// if we got here, there's an error 
					std::cerr << "Warning: failed to guess an interpretation for attr "
						 << attr_lookup(attr) << ", value form " << form_lookup(form) << std::endl;
					return interp::EOL; // FIXME: throw exception instead?				
				default:
					// this means >1
					std::cerr << "Warning: multiple possible interpretations for attr "
						 << attr_lookup(attr) << ", value form " << form_lookup(form) << std::endl;
					return *(possibles.begin());
			}
		}
		
		interp::interp_class guess_interp(const Dwarf_Half attr, const attribute_value& v)
		{
			return get_interp(attr, v.orig_form);
		}
		
		std::ostream& operator<<(std::ostream& s, const attribute_value v)
		{
			v.print_raw(s);
			return s;
		}
		
		std::ostream& operator<<(std::ostream& s, const interp::interp_class cls)
		{
			switch(cls)
			{
				case interp::address: s << "address"; break;
				case interp::block: s << "block"; break;		
				case interp::constant: s << "constant"; break;		
				case interp::lineptr: s << "lineptr"; break;		
				case interp::loclistptr: s << "loclistptr"; break;		
				case interp::macptr: s << "macptr"; break;
				case interp::rangelistptr: s << "rangelistptr"; break;
				case interp::string: s << "string"; break;
				case interp::flag: s << "flag"; break;		
				case interp::reference: s << "reference"; break;
				case interp::block_as_dwarf_expr: s << "block containing DWARF expression"; break;	
				default: s << "(unknown)"; break;
			}
			return s;
		}
		
//		std::ostream& operator<<(std::ostream& s, std::pair<const Dwarf_Half, attribute_value>& entry)
//		{
//			s << "Attribute " << attr_lookup(entry.first) << ", value: ";
//			entry.second.print_as(s, guess_interp(entry.first, entry.second));
//			return s;
//		}
		
		void attribute_value::print_as(std::ostream& s, interp::interp_class cls) const
		{
			switch(cls)
			{
				case interp::address: switch(f)
				{
					case ADDR:
						print_raw(s);
						break;
					default: assert(false);
				} break;				
				case interp::block: switch(f)
				{
					case BLOCK:
						print_raw(s);
						break;
					default: assert(false);
				} break;		
				case interp::constant: switch(f)
				{
					case UNSIGNED:
					case SIGNED:
						print_raw(s);
						break;
					default: assert(false);
				} break;		
				case interp::lineptr: switch(f)
				{
					case UNSIGNED: // and specifically data4 or data8
						s << "(lineptr) 0x" << std::hex << v_u << std::dec;
						break;					
					default: assert(false);
				} break;
				case interp::block_as_dwarf_expr:		
				case interp::loclistptr: switch(f)
				{
					case LOCLIST:
						s << *v_loclist; //"(loclistptr) 0x" << std::hex << v_u << std::dec;
						break;
					default: assert(false);
				} break;		
				case interp::macptr: switch(f)
				{
					case UNSIGNED: // specifically data4 or data8
						s << "(macptr) 0x" << std::hex << v_u << std::dec;
						break;
					default: assert(false);
				} break;		
				case interp::rangelistptr: switch(f)
				{
					case UNSIGNED: // specifically data4 or data8
						s << "(rangelist) 0x" << std::hex << v_u << std::dec;
						break;
					default: assert(false);
				} break;
				case interp::string: switch(f)
				{
					case STRING: // string or strp, we don't care
						print_raw(s);
						break;
					default: assert(false);
				} break;		
				case interp::flag: switch(f)
				{
					case FLAG:
						print_raw(s);
						break;
					default: assert(false);
				} break;		
				case interp::reference: switch(f)
				{
					case REF:
						print_raw(s);
						break;
					default: assert(false);
				} break;
				default: 
					s << "(raw) ";
					print_raw(s);
// 					std::cerr << "Error: couldn't print attribute of form " 
// 						<< form_lookup(orig_form) << " (" << orig_form << ")"
// 						<< " as class " << cls << " (" << static_cast<int>(cls) << ")" << std::endl;
// 					assert(false);
			}			
		} // end attribute_value::print_as
		
		die::die(const die& d) 
			: f(d.f), m_ds(d.m_ds), m_parent(d.m_parent), m_tag(d.m_tag), m_offset(d.m_offset), 
				cu_offset(d.cu_offset),
				m_attrs(d.m_attrs), m_children(d.m_children)
		//, /* rely on std::map copy constructor to deep-copy entries */
		//	lowpc(d.lowpc), highpc(d.highpc), bytesize(d.bytesize), bitsize(d.bitsize),
		//	bitoffset(d.bitoffset), srclang(d.srclang), arrayorder(d.arrayorder)			 
		{ /*std::cerr << "Copy constructing an encap::die" << std::endl;*/ }
				
		die::die(dieset& ds, dwarf::die& d, Dwarf_Off parent_off) 
			: f(d.f), m_ds(ds), m_parent(parent_off)
		{
			/* To encapsulate a DIE, we copy all its properties/attributes,
			 * and encode references to other DIEs and to child DIEs
			 * by their die_offset (i.e. within .debug_info of file f).
			 *
			 * To encapsulate all DIEs in a file, therefore, we build a
			 * map of offsets to encapsulated DIEs. */

			d.tag(&m_tag);
			d.offset(&m_offset);
			d.CU_offset(&cu_offset);
// 			d.lowpc(&lowpc);
// 			d.highpc(&highpc);
// 			d.bytesize(&bytesize);
// 			d.bitsize(&bitsize);
// 			d.bitoffset(&bitoffset);
// 			d.srclang(&srclang);
// 			d.arrayorder(&arrayorder);

			// store a backref, using the magic DW_AT_ 0
			ds.backrefs()[parent_off].push_back(std::make_pair(m_offset, 0));			

			// now for the awkward squad: name and other attributes
			int retval;
			attribute_array arr(d);
			//bool handled;
			for (int i = 0; i < arr.count(); i++)
			{
				Dwarf_Half attr; 
//				try
//				{
					dwarf::attribute a = arr.get(i);
					retval = a.whatattr(&attr);
					m_attrs.insert(std::make_pair(attr, attribute_value(ds, a)));
//				}
//				catch (Error)
//				{
					//std::cerr << "
//					assert(false);
//				}
			} // end for

			// we're done -- don't encode children now; they must be encapsulated separately
		}
		
		die::~die()
		{
			if (m_ds.backrefs().find(m_parent) != m_ds.backrefs().end())
			{
 				dieset::backref_list::iterator found = std::find(
 					m_ds.backrefs()[m_parent].begin(),
 					m_ds.backrefs()[m_parent].end(),
 					std::make_pair(m_offset, (Dwarf_Half) 0));
 				if (found != m_ds.backrefs()[m_parent].end())
 				{
 					m_ds.backrefs()[m_parent].erase(found);
 				}
			}
			else
			{
				// don't assert(false) -- this *might* happen, because
				// the red-black tree structure won't mirror the DWARF tree structure
				// so parents might get destroyed before children
			}
		}
							
		attribute_value::attribute_value(dieset& ds, dwarf::attribute& a)
		{
			int retval;
			retval = a.whatform(&orig_form);
			
// 			attribute_value(Dwarf_Bool b) : f(FLAG), v_flag(b) {}
// 			// HACK to allow overload resolution: addr is ignored
// 			attribute_value(void *addr, Dwarf_Addr value) : f(ADDR), v_addr(value) {}		
// 			attribute_value(Dwarf_Unsigned u) : f(UNSIGNED), v_u(u) {}				
// 			attribute_value(Dwarf_Signed s) : f(SIGNED), v_s(s) {}			
// 			attribute_value(dwarf::block& b) : f(BLOCK), v_block(new std::vector<unsigned char>(
// 					(unsigned char *) b.data(), ((unsigned char *) b.data()) + b.len())) 
// 					{ /*std::cerr << "Constructed a block attribute_value with vector at 0x" << std::hex << (unsigned) v_block << std::dec << std::endl;*/ }			
// 			attribute_value(const char *s) : f(STRING), v_string(new std::string(s)) {}
// 			attribute_value(const std::string& s) : f(STRING), v_string(new std::string(s)) {}				
// 			attribute_value(Dwarf_Off off, bool abs) : f(REF), v_ref(new ref(off, abs)) {}
// 			attribute_value(ref r) : f(REF), v_ref(new ref(r.off, r.abs)) {}
			
			Dwarf_Unsigned u;
			Dwarf_Signed s;
			Dwarf_Bool flag;
			Dwarf_Off o;
			Dwarf_Addr addr;
			char *str;
			interp::interp_class cls = interp::EOL; // dummy initialization
			if (retval != DW_DLV_OK) goto fail; // retval set by whatform() above
			Dwarf_Half attr; retval = a.whatattr(&attr);
			if (retval != DW_DLV_OK) goto fail;
			cls = get_interp(attr, orig_form);
						
			switch(cls)
			{
				case interp::string:
					a.formstring(&str);
					this->f = STRING; 
					this->v_string = new std::string(str);
					break;
				case interp::flag:
					a.formflag(&flag);
					this->f = FLAG;
					this->v_flag = flag;
					break;
				case interp::address:
					a.formaddr(&addr);
					this->f = ADDR;
					this->v_addr = addr;
					break;
				case interp::block:
					{
						block b(a);
						this->f = BLOCK;
						this->v_block = new std::vector<unsigned char>(
		 					(unsigned char *) b.data(), ((unsigned char *) b.data()) + b.len());
					}
					break;
				case interp::reference:
					this->f = REF;
					Dwarf_Off  referencing_off; 
					a.get_containing_array().get_containing_die().offset(&referencing_off);
					Dwarf_Half referencing_attr;
					a.whatattr(&referencing_attr);
					if (orig_form == DW_FORM_ref_addr)
					{
						a.formref_global(&o);
						this->v_ref = new ref(ds, o, true, referencing_off, referencing_attr);
					}
					else
					{
						a.formref(&o);
						this->v_ref = new ref(ds, o, false, referencing_off, referencing_attr);
					}
					break;
				as_if_unsigned:
				case interp::constant:
					if (orig_form == DW_FORM_sdata)
					{
						a.formsdata(&s);
						this->f = SIGNED;
						this->v_s = s;					
					}
					else
					{
						a.formudata(&u);
						this->f = UNSIGNED;
						this->v_u = u;
					}
					break;
				case interp::block_as_dwarf_expr: // dwarf_loclist_n works for both of these
				case interp::loclistptr:
					this->f = LOCLIST;
					this->v_loclist = new loclist(dwarf::loclist(a));
					break;
				case interp::lineptr:
				case interp::macptr:
				case interp::rangelistptr:
					goto as_if_unsigned;		
				fail:
				default:
					// FIXME: we failed to case-catch, or handle, the FORM; do something
					std::cerr << "FIXME: didn't know how to handle an attribute of form "
						<< form_lookup(orig_form) << std::endl;
					assert(false);
					break;
			}
		}
		
		attribute_value::ref::ref(dieset& ds, Dwarf_Off off, bool abs, 
			Dwarf_Off referencing_off, Dwarf_Half referencing_attr)	
				: off(off), abs(abs), ds(ds),
					referencing_off(referencing_off), referencing_attr(referencing_attr)
		{
			ds.backrefs()[off].push_back(std::make_pair(referencing_off, referencing_attr));
		}

		attribute_value::ref::~ref()
		{
			/* Find the corresponding backreference entry (associated with 
			 * the target of this reference),
			 * and remove it if found.
			 * 
			 * Sometimes, off will not be found in backrefs.  How can this happen? 
			 * When destroying a dieset, the backrefs map is destroyed first.
			 * This simply removes each vector of <Half, Half> pairs in some order.
			 * Then the attribute values are destroyed. These are still trying to
			 * preserve backrefs integrity---but they can't, because backrefs has
			 * deallocated all its state. The backrefs object is still functional
			 * though, even though its destructor has completed... so we can
			 * hopefully test for presence of off. */
			if (ds.backrefs().find(off) != ds.backrefs().end())
			{
 				dieset::backref_list::iterator found = std::find(
 					ds.backrefs()[off].begin(),
 					ds.backrefs()[off].end(),
 					std::make_pair(referencing_off, referencing_attr));
 				if (found != ds.backrefs()[off].end())
 				{
 					ds.backrefs()[off].erase(found);
 				}
			}
		}
		attribute_value::ref::ref(const ref& r) : off(r.off), abs(r.abs), ds(r.ds),
			referencing_off(r.referencing_off), referencing_attr(r.referencing_attr)  // copy constructor
		{
			ds.backrefs()[off].push_back(std::make_pair(referencing_off, referencing_attr));
		}
		
		const attribute_value *attribute_value::dne_val;
		
		std::ostream& operator<<(std::ostream& o, const dwarf::encap::die& d)
		{
 			//o << "TAG: " << tag_lookup(d.m_tag) << std::endl;
// 				o << "LOWPC: " << (*this)[DW_TAG_lowpc] << std::endl;
// 				o << "HIGHPC: " << (*this)[DW_TAG_highpc] << std::endl;
// 				o << "BYTESIZE: " << (*this)[DW_TAG_bytesize] << std::endl;
// 				o << "BITSIZE: " << (*this)[DW_TAG_bitsize] << std::endl;
// 				o << "BITOFFSET: " << (*this)[DW_TAG_bitoffset] << std::endl;
// 				o << "SRCLANG: " << (*this)[DW_TAG_srclang] << std::endl;
			o 	<< "DIE, child of 0x" << std::hex << d.m_parent << std::dec 
				<< ", tag: " << tag_lookup(d.m_tag) 
				<< ", offset: 0x" << std::hex << d.m_offset << std::dec 
				<< ", name: "; if (d.has_attr(DW_AT_name)) o << d[DW_AT_name]; else o << "(no name)"; o << std::endl;
			//o << "ATTTRIBUTES: " << std::endl;
// 			for (std::map<Dwarf_Half, attribute_value>::iterator p = d.m_attrs.begin();
// 					p != d.m_attrs.end(); p++)
// 			{
// 				o << "Attribute " << attr_lookup(p->first) << ", value: ";
// 				p->second.print(o);
// 				o << std::endl;
// 			}

			for (std::map<Dwarf_Half, encap::attribute_value>::const_iterator p 
					= d.const_attrs().begin();
				p != d.const_attrs().end(); p++)
			{
				//for (int i = 0; i < indent + 1; i++) 
				o << "\t";

			 


				o << "Attribute " << attr_lookup(p->first) << ", value: ";
				p->second.print_as(o, guess_interp(p->first, p->second));

				//o << attr_lookup(p->first);
				//o << p->second;
				o << std::endl;	
			}			

			return o;
		}
	} // end namespace encap
	
	void evaluator::eval()
	{
		std::vector<Dwarf_Loc>::iterator i = expr.m_expr.begin();
		while (i != expr.m_expr.end())
		{
			switch(i->lr_atom)
			{
				case DW_OP_constu:
					stack.push(i->lr_number);
					i++;
					break;
				default:
					std::cerr << "Error: unrecognised opcode: " << op_lookup(i->lr_atom) << std::endl;
					assert(false);					
			}
		}
	}	
} // end namespace dwarf

bool operator==(const dwarf::encap::expr_instr& e1, const dwarf::encap::expr_instr& e2)
{
	return e1.lr_atom == e2.lr_atom
		&& e1.lr_number == e2.lr_number
		&& e1.lr_number2 == e2.lr_number2
		&& e1.lr_offset == e2.lr_offset;
}
bool operator!=(const dwarf::encap::expr_instr& e1, const dwarf::encap::expr_instr& e2)
{
	return !(e1 == e2);
}
