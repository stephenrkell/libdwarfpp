/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * spec.cpp: tables describing DWARF standards and vendor extensions.
 *
 * Copyright (c) 2008--13, Stephen Kell.
 */

#include "dwarfpp/spec.hpp"
#include <vector>

using std::cerr;
using std::endl;
using std::string;
using std::vector;
using std::map;
using std::make_pair; 

namespace dwarf
{
	namespace spec
	{
		const int empty_def_t::empty_operand_form_list[] = { 0 };
		const int empty_def_t::empty_class_list[] = { interp::EOL };
		
		int explicit_interp(abstract_def& def, int attr, const int *attr_possible_classes, int form, const int *form_possible_classes)
		{
			vector<int> possibles;

			// null pointer means the empty list
			if (attr_possible_classes == 0
				&& form_possible_classes == 0) goto fail;

			// one or other pointer is a list, terminated by interp::EOL

			/* Fudge: to avoid bombing out when encountering future DWARF standards 
			 * or vendor extensions, we want to be able to handle unknown attributes
			 * or unknown forms as far as possible. Therefore, we should tolerate
			 * cases where either attr_possible_classes *or* form_possible_classes
			 * are empty (but not both), *and* the nonempty one is a singleton. The
			 * singleton effectively determines that at best, one interpretation is
			 * possible, so we return it. It may be erroneous to do so; at worst this 
			 * just means we return a bogus (but hopefully not unsafe) interpretation of 
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
					possibles.push_back(attr_possible_classes[0] & ~interp::FLAGS);
			}
			else
			{
				// if we hit this block, either neither list is empty, or both are

				// find the intersection of these two lists
				for (const int *p_attr_cls = attr_possible_classes;
					p_attr_cls != 0 && *p_attr_cls != interp::EOL; ++p_attr_cls)
				{
					// just return the first possible interpretation
					for (const int *p_form_cls = form_possible_classes;
						p_form_cls != 0 && *p_form_cls != interp::EOL; ++p_form_cls)
					{
						if (*p_form_cls == (*p_attr_cls & ~interp::FLAGS)) possibles.push_back(*p_form_cls);
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
					cerr << "Warning: failed to guess an interpretation for attr "
						 << def.attr_lookup(attr) << ", value form " << def.form_lookup(form) << endl;
					return spec::interp::EOL; // FIXME: throw exception instead?				
				default:
					// this means >1
					cerr << "Warning: multiple possible interpretations for attr "
						 << def.attr_lookup(attr) << ", value form " << def.form_lookup(form) << ": { "; 
					for (auto i_poss = possibles.begin(); i_poss != possibles.end(); ++i_poss)
					{
						if (i_poss != possibles.begin()) cerr << ", ";
						cerr << *i_poss;
					}
					cerr << " }" << endl;
					return *(possibles.begin());
			}
		}
		
		typedef std::pair<const char *, int> forward_name_mapping_t;
		typedef std::pair<int, const char *> inverse_name_mapping_t;
		typedef std::pair<int, const int *> attr_class_mapping_t;       
		typedef std::pair<int, const int *> form_class_mapping_t;       
		typedef std::pair<int, const int *> op_operand_forms_mapping_t;

#define DEFINE_MAPS(classname) \
							const std::map<const char *, int, string_comparator> classname::tag_forward_map(&tag_forward_tbl[0], &tag_forward_tbl[sizeof tag_forward_tbl / sizeof (forward_name_mapping_t)]); \
							const std::map<int, const char *> classname::tag_inverse_map(&tag_inverse_tbl[0], &tag_inverse_tbl[sizeof tag_inverse_tbl / sizeof (inverse_name_mapping_t)]); \
							const std::map<const char *, int, string_comparator> classname::attr_forward_map(&attr_forward_tbl[0], &attr_forward_tbl[sizeof attr_forward_tbl / sizeof (forward_name_mapping_t)]); \
							const std::map<int, const char *> classname::attr_inverse_map(&attr_inverse_tbl[0], &attr_inverse_tbl[sizeof attr_inverse_tbl / sizeof (inverse_name_mapping_t)]); \
							const std::map<const char *, int, string_comparator> classname::form_forward_map(&form_forward_tbl[0], &form_forward_tbl[sizeof form_forward_tbl / sizeof (forward_name_mapping_t)]); \
							const std::map<int, const char *> classname::form_inverse_map(&form_inverse_tbl[0], &form_inverse_tbl[sizeof form_inverse_tbl / sizeof (inverse_name_mapping_t)]); \
							const std::map<const char *, int, string_comparator> classname::encoding_forward_map(&encoding_forward_tbl[0], &encoding_forward_tbl[sizeof encoding_forward_tbl / sizeof (forward_name_mapping_t)]); \
							const std::map<int, const char *> classname::encoding_inverse_map(&encoding_inverse_tbl[0], &encoding_inverse_tbl[sizeof encoding_inverse_tbl / sizeof (inverse_name_mapping_t)]); \
							const std::map<const char *, int, string_comparator> classname::op_forward_map(&op_forward_tbl[0], &op_forward_tbl[sizeof op_forward_tbl / sizeof (forward_name_mapping_t)]); \
							const std::map<int, const char *> classname::op_inverse_map(&op_inverse_tbl[0], &op_inverse_tbl[sizeof op_inverse_tbl / sizeof (inverse_name_mapping_t)]); \
							const std::map<const char *, int, string_comparator> classname::interp_forward_map(&interp_forward_tbl[0], &interp_forward_tbl[sizeof interp_forward_tbl / sizeof (forward_name_mapping_t)]); \
							const std::map<int, const char *> classname::interp_inverse_map(&interp_inverse_tbl[0], &interp_inverse_tbl[sizeof interp_inverse_tbl / sizeof (inverse_name_mapping_t)]); \
							const std::map<int, const int *>  classname::op_operand_forms_map(&op_operand_forms_tbl[0], &op_operand_forms_tbl[sizeof op_operand_forms_tbl / sizeof (op_operand_forms_mapping_t)]); \
							const std::map<int, const int *>  classname::attr_class_map(&attr_class_tbl[0], &attr_class_tbl[sizeof attr_class_tbl / sizeof (attr_class_mapping_t)]); \
							const std::map<int, const int *>  classname::form_class_map(&form_class_tbl[0], &form_class_tbl[sizeof form_class_tbl / sizeof (form_class_mapping_t)]);

// generic list-making macros
#define PAIR_ENTRY_FORWARDS(sym) (std::make_pair(#sym, (sym))),
#define PAIR_ENTRY_BACKWARDS(sym) (std::make_pair((sym), #sym)),
#define PAIR_ENTRY_FORWARDS_LAST(sym) (std::make_pair(#sym, (sym)))
#define PAIR_ENTRY_BACKWARDS_LAST(sym) (std::make_pair((sym), #sym))

#define PAIR_ENTRY_QUAL_FORWARDS(qual, sym) (std::make_pair(#sym, (qual sym))),
#define PAIR_ENTRY_QUAL_BACKWARDS(qual, sym) (std::make_pair((qual sym), #sym)),
#define PAIR_ENTRY_QUAL_FORWARDS_LAST(qual, sym) (std::make_pair(#sym, (qual sym)))
#define PAIR_ENTRY_QUAL_BACKWARDS_LAST(qual, sym) (std::make_pair((qual sym), #sym))

#define PAIR_ENTRY_FORWARDS_VARARGS(sym, ...) (std::make_pair(#sym, (sym))),
#define PAIR_ENTRY_BACKWARDS_VARARGS(sym, ...) (std::make_pair((sym), #sym)),
#define PAIR_ENTRY_FORWARDS_VARARGS_LAST(sym, ...) (std::make_pair(#sym, (sym)))
#define PAIR_ENTRY_BACKWARDS_VARARGS_LAST(sym, ...) (std::make_pair((sym), #sym))

#define MAKE_LOOKUP(pair_type, name, make_pair, last_pair, list) \
pair_type name[] = { \
						list(make_pair, last_pair) \
					}

		/* DWARF4: tags */
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
							make_pair(DW_TAG_shared_type) /* DWARF3f */ \
							make_pair(DW_TAG_type_unit) /* DWARF4 */ \
							make_pair(DW_TAG_rvalue_reference_type) /* DWARF4 */ \
							last_pair(DW_TAG_template_alias) /* DWARF4 */

		MAKE_LOOKUP(forward_name_mapping_t, tag_forward_tbl, PAIR_ENTRY_FORWARDS, PAIR_ENTRY_FORWARDS_LAST, TAG_PAIR_LIST);
		MAKE_LOOKUP(inverse_name_mapping_t, tag_inverse_tbl, PAIR_ENTRY_BACKWARDS, PAIR_ENTRY_BACKWARDS_LAST, TAG_PAIR_LIST);

		bool dwarf4_t::local_tag_is_type(int tag) const
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
				|| tag == DW_TAG_shared_type
				|| tag == DW_TAG_thrown_type
				|| tag == DW_TAG_rvalue_reference_type;
		}
		bool dwarf4_t::local_tag_is_type_chain(int tag) const
		{
			return tag == DW_TAG_typedef
				|| tag == DW_TAG_const_type
				|| tag == DW_TAG_packed_type
				|| tag == DW_TAG_volatile_type
				|| tag == DW_TAG_restrict_type
				|| tag == DW_TAG_shared_type
				|| tag == DW_TAG_thrown_type
				|| tag == DW_TAG_rvalue_reference_type;
		}
		/* This predicate is intended to match DWARF entries on which we might do
		 * Cake's "name : pred"-style checking/assertions, where the 'name'd elements
		 * are *immediate* child DIEs -- *not* DIEs referenced by offset in attributes. */
		bool dwarf4_t::local_tag_has_named_children(int tag) const
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

		/* DWARF4: attributes */
		#define ATTR_DECL_LIST(make_decl, last_decl) \
							make_decl(DW_AT_sibling, interp::reference ) \
							make_decl(DW_AT_location, interp::loclistptr, /* GNU HACK */ interp::block, interp::exprloc ) \
							make_decl(DW_AT_name, interp::string ) \
							make_decl(DW_AT_ordering, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_byte_size, interp::constant|interp::UNSIGNED, interp::reference, interp::exprloc ) \
							make_decl(DW_AT_bit_offset, interp::constant|interp::UNSIGNED, interp::reference, interp::exprloc ) \
							make_decl(DW_AT_bit_size, interp::constant|interp::UNSIGNED, interp::reference, interp::exprloc ) \
							make_decl(DW_AT_stmt_list, interp::lineptr ) \
							make_decl(DW_AT_low_pc, interp::address ) \
							make_decl(DW_AT_high_pc, interp::address, interp::constant ) \
							make_decl(DW_AT_language, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_discr, interp::reference ) \
							make_decl(DW_AT_discr_value, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_visibility, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_import, interp::reference ) \
							make_decl(DW_AT_string_length, interp::loclistptr, interp::exprloc ) \
							make_decl(DW_AT_common_reference, interp::reference ) \
							make_decl(DW_AT_comp_dir, interp::string ) \
							make_decl(DW_AT_const_value, interp::block, interp::constant|interp::UNSIGNED, interp::string ) \
							make_decl(DW_AT_containing_type, interp::reference ) \
							make_decl(DW_AT_default_value, interp::reference ) \
							make_decl(DW_AT_inline, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_is_optional, interp::flag ) \
							make_decl(DW_AT_lower_bound, interp::constant|interp::UNSIGNED, interp::reference, interp::exprloc ) \
							make_decl(DW_AT_producer, interp::string ) \
							make_decl(DW_AT_prototyped, interp::flag ) \
							make_decl(DW_AT_return_addr, interp::loclistptr, interp::exprloc ) \
							make_decl(DW_AT_start_scope, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_bit_stride, interp::constant|interp::UNSIGNED, interp::exprloc ) \
							make_decl(DW_AT_upper_bound, interp::constant|interp::UNSIGNED, interp::reference, interp::exprloc ) \
							make_decl(DW_AT_abstract_origin, interp::reference ) \
							make_decl(DW_AT_accessibility, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_address_class, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_artificial, interp::flag ) \
							make_decl(DW_AT_base_types, interp::reference ) \
							make_decl(DW_AT_calling_convention, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_count, interp::constant|interp::UNSIGNED, interp::reference, interp::exprloc ) \
							make_decl(DW_AT_data_member_location, interp::loclistptr, interp::constant|interp::UNSIGNED, interp::exprloc, /* GNU HACK */ interp::block ) \
							make_decl(DW_AT_decl_column, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_decl_file, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_decl_line, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_declaration, interp::flag ) \
							make_decl(DW_AT_discr_list, interp::block ) \
							make_decl(DW_AT_encoding, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_external, interp::flag ) \
							make_decl(DW_AT_frame_base, interp::loclistptr, interp::exprloc ) \
							make_decl(DW_AT_friend, interp::reference ) \
							make_decl(DW_AT_identifier_case, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_macro_info, interp::reference, interp::macptr ) \
							make_decl(DW_AT_namelist_item, interp::reference ) \
							make_decl(DW_AT_priority, interp::reference ) \
							make_decl(DW_AT_segment, interp::loclistptr, interp::exprloc ) \
							make_decl(DW_AT_specification, interp::reference ) \
							make_decl(DW_AT_static_link, interp::loclistptr, interp::exprloc ) \
							make_decl(DW_AT_type, interp::reference ) \
							make_decl(DW_AT_use_location, interp::loclistptr, interp::exprloc ) \
							make_decl(DW_AT_variable_parameter, interp::flag ) \
							make_decl(DW_AT_virtuality, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_vtable_elem_location, interp::loclistptr, interp::exprloc ) \
							make_decl(DW_AT_allocated, interp::constant|interp::UNSIGNED, interp::reference ) \
							make_decl(DW_AT_associated, interp::constant|interp::UNSIGNED, interp::reference, interp::exprloc ) \
							make_decl(DW_AT_data_location, interp::exprloc ) \
							make_decl(DW_AT_byte_stride, interp::constant|interp::UNSIGNED, interp::reference, interp::exprloc ) \
							make_decl(DW_AT_entry_pc, interp::address ) \
							make_decl(DW_AT_use_UTF8, interp::flag ) \
							make_decl(DW_AT_extension, interp::reference ) \
							make_decl(DW_AT_ranges, interp::rangelistptr ) \
							make_decl(DW_AT_trampoline, interp::address, interp::flag, interp::reference, interp::string ) \
							make_decl(DW_AT_call_column, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_call_file, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_call_line, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_description, interp::string ) \
							make_decl(DW_AT_binary_scale, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_decimal_scale, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_small, interp::reference ) \
							make_decl(DW_AT_decimal_sign, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_digit_count, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_picture_string, interp::string ) \
							make_decl(DW_AT_mutable, interp::flag ) \
							make_decl(DW_AT_threads_scaled, interp::flag ) \
							make_decl(DW_AT_explicit, interp::flag ) \
							make_decl(DW_AT_object_pointer, interp::reference ) \
							make_decl(DW_AT_endianity, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_elemental, interp::flag ) \
							make_decl(DW_AT_pure, interp::flag ) \
							make_decl(DW_AT_recursive, interp::flag ) \
							make_decl(DW_AT_signature, interp::reference ) \
							make_decl(DW_AT_main_subprogram, interp::flag ) \
							make_decl(DW_AT_data_bit_offset, interp::constant|interp::UNSIGNED ) \
							make_decl(DW_AT_const_expr, interp::flag ) \
							make_decl(DW_AT_enum_class, interp::flag ) \
							last_decl(DW_AT_linkage_name, interp::string )

		MAKE_LOOKUP(forward_name_mapping_t, attr_forward_tbl, PAIR_ENTRY_FORWARDS_VARARGS, PAIR_ENTRY_FORWARDS_VARARGS_LAST, ATTR_DECL_LIST);
		MAKE_LOOKUP(inverse_name_mapping_t, attr_inverse_tbl, PAIR_ENTRY_BACKWARDS_VARARGS, PAIR_ENTRY_BACKWARDS_VARARGS_LAST, ATTR_DECL_LIST);

		bool dwarf4_t::local_attr_describes_location(int attr) const
		{
			return attr == DW_AT_location
				|| attr == DW_AT_data_member_location
				|| attr == DW_AT_vtable_elem_location
				|| attr == DW_AT_string_length
				|| attr == DW_AT_use_location
				|| attr == DW_AT_return_addr;
		}

		// define an array of interpretation classes for each DW_AT_ constant
		#define make_attr_array_declaration(attr_name, ...) const int attr_classes_ ## attr_name[] = { __VA_ARGS__, interp::EOL }; //
		ATTR_DECL_LIST(make_attr_array_declaration, make_attr_array_declaration)

		// define a lookup table from DW_AT_ constants to the arrays just created
		#define make_attr_associative_entry(attr_name, ...) make_pair(attr_name, attr_classes_ ## attr_name),
		#define last_attr_associative_entry(attr_name, ...) make_pair(attr_name, attr_classes_ ## attr_name)
		MAKE_LOOKUP(attr_class_mapping_t, attr_class_tbl, make_attr_associative_entry, last_attr_associative_entry, ATTR_DECL_LIST);

		#define FORM_DECL_LIST(make_decl, last_decl) \
							make_decl(DW_FORM_addr, interp::address, interp::constant|interp::UNSIGNED)  \
							make_decl(DW_FORM_block2, interp::block)  \
							make_decl(DW_FORM_block4, interp::block)  \
							make_decl(DW_FORM_data2, interp::constant)  \
							make_decl(DW_FORM_data4, interp::constant, /* GNU HACK */ interp::loclistptr, interp::rangelistptr, interp::lineptr, interp::macptr)  \
							make_decl(DW_FORM_data8, interp::constant, /* GNU HACK */ interp::loclistptr, interp::rangelistptr, interp::lineptr, interp::macptr)  \
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
							make_decl(DW_FORM_indirect, interp::EOL) \
							make_decl(DW_FORM_sec_offset, interp::reference, interp::lineptr, interp::loclistptr, interp::macptr, interp::rangelistptr) \
							make_decl(DW_FORM_exprloc, interp::exprloc, interp::EOL ) \
							make_decl(DW_FORM_flag_present, interp::flag ) \
							last_decl(DW_FORM_ref_sig8, interp::reference ) 


		MAKE_LOOKUP(forward_name_mapping_t, form_forward_tbl, PAIR_ENTRY_FORWARDS_VARARGS, PAIR_ENTRY_FORWARDS_VARARGS_LAST, FORM_DECL_LIST);
		MAKE_LOOKUP(inverse_name_mapping_t, form_inverse_tbl, PAIR_ENTRY_BACKWARDS_VARARGS, PAIR_ENTRY_BACKWARDS_VARARGS_LAST, FORM_DECL_LIST);

		// define an array of interpretation classes for each DW_FORM_ constant
		#define make_form_array_declaration(form_name, ...) const int form_classes_ ## form_name[] = { __VA_ARGS__, interp::EOL }; //
		FORM_DECL_LIST(make_form_array_declaration, make_form_array_declaration);

		// define a lookup table from DW_FORM_ constants to the arrays just created
		#define make_form_associative_entry(form_name, ...) make_pair(form_name, form_classes_ ## form_name),
		#define last_form_associative_entry(form_name, ...) make_pair(form_name, form_classes_ ## form_name)
		MAKE_LOOKUP(form_class_mapping_t, form_class_tbl, make_form_associative_entry, last_form_associative_entry, FORM_DECL_LIST);
/* temporary HACK while dwarf.h catches up. */
#ifndef DW_ATE_utf
#define DW_ATE_utf 0x10
#endif
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
							make_pair(DW_ATE_decimal_float) \
							last_pair(DW_ATE_utf) 
		MAKE_LOOKUP(forward_name_mapping_t, encoding_forward_tbl, PAIR_ENTRY_FORWARDS, PAIR_ENTRY_FORWARDS_LAST, ENCODING_PAIR_LIST);
		MAKE_LOOKUP(inverse_name_mapping_t, encoding_inverse_tbl, PAIR_ENTRY_BACKWARDS, PAIR_ENTRY_BACKWARDS_LAST, ENCODING_PAIR_LIST);

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
						make_decl(DW_OP_regx, DW_FORM_udata) \
						make_decl(DW_OP_fbreg, DW_FORM_sdata) \
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
						make_decl(DW_OP_implicit_value, 2) \
						make_decl(DW_OP_stack_value, 0) /*\
						make_decl(DW_OP_GNU_push_tls_address, 0) \
						make_decl(DW_OP_HP_is_value, 0) \
						make_decl(DW_OP_HP_fltconst4, 0) \
						make_decl(DW_OP_HP_fltconst8, 0) \
						make_decl(DW_OP_HP_mod_range, 0) \
						make_decl(DW_OP_HP_unmod_range, 0) \
						make_decl(DW_OP_HP_tls, 0) \
						last_decl(DW_OP_hi_user, 0) */

		bool dwarf4_t::local_op_reads_register(int op) const
		{
			return (op >= DW_OP_reg0 && op <= DW_OP_bregx)
				|| op == DW_OP_fbreg;
		}
		MAKE_LOOKUP(forward_name_mapping_t, op_forward_tbl, PAIR_ENTRY_FORWARDS_VARARGS, PAIR_ENTRY_FORWARDS_VARARGS_LAST, OP_DECL_LIST);
		MAKE_LOOKUP(inverse_name_mapping_t, op_inverse_tbl, PAIR_ENTRY_BACKWARDS_VARARGS, PAIR_ENTRY_BACKWARDS_VARARGS_LAST, OP_DECL_LIST);

		// define an array of interpretation classes for each DW_FORM_ constant
		#define make_op_array_declaration(op_name, ...) const int op_operand_forms_ ## op_name[] = { __VA_ARGS__, 0 }; //
		OP_DECL_LIST(make_op_array_declaration, make_op_array_declaration);

		// define a lookup table from DW_OP_ constants to the arrays just created
		#undef make_op_associative_entry
		#undef last_op_associative_entry
		#define make_op_associative_entry(op_name, ...) make_pair(op_name, op_operand_forms_ ## op_name),
		#define last_op_associative_entry(op_name, ...) make_pair(op_name, op_operand_forms_ ## op_name)
		MAKE_LOOKUP(op_operand_forms_mapping_t, op_operand_forms_tbl, make_op_associative_entry, last_op_associative_entry, OP_DECL_LIST);

		#define INTERP_DECL_LIST(make_decl, last_decl) \
						make_decl(interp::, EOL) \
						make_decl(interp::, reference) \
						make_decl(interp::, block) \
						make_decl(interp::, loclistptr) \
						make_decl(interp::, string) \
						make_decl(interp::, constant) \
						make_decl(interp::, lineptr) \
						make_decl(interp::, address) \
						make_decl(interp::, flag) \
						make_decl(interp::, macptr) \
						make_decl(interp::, rangelistptr) \
						make_decl(interp::, block_as_dwarf_expr) \

		MAKE_LOOKUP(forward_name_mapping_t, interp_forward_tbl, PAIR_ENTRY_QUAL_FORWARDS, PAIR_ENTRY_QUAL_FORWARDS_LAST, INTERP_DECL_LIST);
		MAKE_LOOKUP(inverse_name_mapping_t, interp_inverse_tbl, PAIR_ENTRY_QUAL_BACKWARDS, PAIR_ENTRY_QUAL_BACKWARDS_LAST, INTERP_DECL_LIST);

		/* Sometimes, information is encoded into a DWARF attribute in a way which isn't
		 * captured by the official attr->classes and form->classes tables.
		 * This function hardcodes knowledge about this. 
		 * NOTE that the one known case of this (blocks encoding locations) 
		 * was fixed in DWARF4 (using DW_FORM_exprloc), 
		 * but we of course keep the code here for 
		 * compatibility. */

		int dwarf4_t::get_interp(int attr, int form) const 
		{
			int cls = this->get_explicit_interp(attr, form);
			switch(cls)
			{
				case interp::block:
					if (attr_describes_location(attr)) return spec::interp::block_as_dwarf_expr;
					else return cls;
				case interp::constant:
					if (attr == DW_AT_data_member_location)
					{
						/* In DWARF4, DW_AT_data_member_locations can be 
						 * encoded as simple offsets. We turn them into
						 * location expressions. */
						return spec::interp::constant_to_make_location_expr;
					} else return cls;
				default:
					return cls;
			}
		}
		
		 /* FIXME: init priority might be an issue -- need to make sure that we 
		  * initialize this before any static state that uses DWARF specs in this 
		  * library. BUT note that static state in *other* libraries is not our 
		  * problem -- client programmers must get the link order correct so that 
		  * initialization of client objects happens after this library is init'd. */
		
		empty_def_t empty_def_t::inst;

		DEFINE_MAPS(dwarf4_t)
		dwarf4_t dwarf4_t::inst;
		dwarf4_t& dwarf4 = dwarf4_t::inst;
		
		abstract_def& dwarf_current = dwarf4_t::inst; /* HACK */
		
		abstract_def& DEFAULT_DWARF_SPEC = dwarf4;
		abstract_def& DEFAULT_SPEC = dwarf4;
	
		void print_symmetric_map_pair(
			std::ostream& o,
			const map<const char *, int>& forward_map,
			const map<int, const char *>& inverse_map)
		{
			for (map<const char *, int>::const_iterator i = forward_map.begin();
					i != forward_map.end();
					i++)
			{
				o << i->first << ": 0x" << std::hex << i->second << std::dec << std::endl;
				//assert(std::find(inverse_map.begin(),
				//		inverse_map.end(), 
				 //   	std::make_pair<const int, const char *>(i->second, i->first))
				//	!= inverse_map.end());
			}		
		}

		std::ostream& operator<<(std::ostream& o, const abstract_def& a)
		{
			return a.print(o);
		}
	} // end namespace spec
} // end namespace dwarf
