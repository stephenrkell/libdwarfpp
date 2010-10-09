# These describe the abstract contents of a given DWARF attribute.
# We assume that once libdwarf has done its abstraction, exactly one
# concrete representation will suffice for any given attribute. This
# means that attributes which can be represented in a variety of forms
# (e.g. location-describing attributes, that can be constants or full
# DWARF expressions) have all been normalised by the time our interface
# sees them (in either direction).
#
# Note that these are specific to a DWARF standard -- alternative standards
# can and do add to these!
attr_types = [ \
("sibling", "refdie" ), \
("location", "loclist" ), \
("name", "string"), \
("ordering", "FIXME"), \
("byte_size", "unsigned"), \
("bit_offset", "unsigned" ), \
("bit_size", "unsigned" ), \
("stmt_list", "FIXME" ), \
("low_pc", "address" ), \
("high_pc", "address" ), \
("language", "unsigned" ), \
("discr", "refdie" ), \
("discr_value", "signed" ), \
("visibility", "unsigned" ), \
("import", "refdie" ), \
("string_length", "loclist" ), \
("common_reference", "refdie" ), \
("comp_dir", "string" ), \
("const_value", "signed" ), \
("containing_type", "refdie" ), \
("default_value", "refdie" ), \
("inline", "signed" ), \
("is_optional", "flag" ), \
("lower_bound", "unsigned" ), \
("producer", "string" ), \
("prototyped", "flag" ), \
("return_addr", "loclist" ), \
("start_scope", "FIXME" ), \
("bit_stride", "signed" ), \
("upper_bound", "unsigned" ), \
("abstract_origin", "refdie" ), \
("accessibility", "unsigned" ), \
("address_class", "unsigned" ), \
("artificial", "flag" ), \
("base_types", "refdie" ), \
("calling_convention", "unsigned" ), \
("count", "unsigned" ), \
("data_member_location", "loclist" ), \
("decl_column", "unsigned" ), \
("decl_file", "unsigned" ), \
("decl_line", "unsigned" ), \
("declaration", "flag" ), \
("discr_list", "FIXME" ), \
("encoding", "unsigned" ), \
("external", "flag" ), \
("frame_base", "loclist" ), \
("friend", "refdie" ), \
("identifier_case", "unsigned" ), \
("macro_info", "offset" ), \
("namelist_item", "refdie" ), \
("priority", "refdie" ), \
("segment", "loclist" ), \
("specification", "refdie" ), \
("static_link", "loclist" ), \
("type", "refdie_is_type" ), \
("use_location", "loclist" ), \
("variable_parameter", "flat" ), \
("virtuality", "unsigned" ), \
("vtable_elem_location", "loclist" ), \
("allocated", "refdie" ), \
("associated", "refdie" ), \
("data_location", "loclist" ), \
("byte_stride", "unsigned" ), \
("entry_pc", "address" ), \
("use_UTF8", "flag" ), \
("extension", "refdie" ), \
("ranges", "rangelist" ), \
("trampoline", "FIXME" ), \
("call_column", "unsigned" ), \
("call_file", "unsigned" ), \
("call_line", "unsigned" ), \
("description", "string" ), \
("binary_scale", "FIXME" ), \
("decimal_scale", "FIXME" ), \
("small", "refdie" ), \
("decimal_sign", "FIXME" ), \
("digit_count", "FIXME" ), \
("picture_string", "FIXME" ), \
("mutable", "flag" ), \
("threads_scaled", "flag" ), \
("explicit", "flag" ), \
("object_pointer", "refdie" ), \
("endianity", "unsigned" ), \
("elemental", "flag" ), \
("pure", "flag" ) \
] 
attr_type_map = dict(attr_types)

# artificial superclasses
# 
artificial_tags = [ \
("basic", ([], [], []) ),
("program_element", ([("decl_column", False ), ("decl_file", False), ("decl_line", False), ("prototyped", False), ("declaration", False), ("external", False), ("visibility", False)], [], ["basic"]) ), \
("type", ([("byte_size", False )], [], ["program_element"]) ), \
("type_chain", ([("type", False)],  [], ["type"]) ), \
("with_named_children", ([], [], ["basic"])), \
("with_runtime_location", ([], [], ["basic"])), \
("with_stack_location", ([], [], ["basic"])) \
]
artificial_tag_map = dict(artificial_tags)

# abbreviations
member_types = [ "class_type", "typedef", "structure_type", "enumeration_type", "union_type" ]

tags = [ \
("array_type", ( [("type", False)], ["subrange_type"], ["type"] ) ), \
("class_type", ( [], [ "member", "access_declaration" ] + member_types, ["type", "with_named_children"] ) ), \
("entry_point", ( [], [] , ["basic"] ) ), \
("enumeration_type", ( [("type", False)], ["enumerator"] , ["type", "with_named_children"] ) ), \
("formal_parameter", ( [("type", False), ("location", False) ], [] , ["program_element", "with_stack_location"] ) ), \
("imported_declaration", ( [], [], ["basic"]  ) ), \
("label", ( [], [], ["basic"]  ) ), \
("lexical_block", ( [("low_pc", False), ("high_pc", False), ("ranges", False)], [ "variable" ] , ["with_runtime_location"] ) ), \
("member", ( [("type", False), ("data_member_location", False)], [], ["basic"]  ) ), \
("pointer_type", ( [], [], ["type_chain"]  ) ), \
("reference_type", ( [], [], ["type_chain"]  ) ), \
("compile_unit", ( [ ("language", True), ("comp_dir", False), ("low_pc", False), ("high_pc", False), ("ranges", False)], [ "subprogram", "variable", "base_type", "pointer_type", "reference_type" ] + member_types, ["with_named_children", "with_runtime_location"]  ) ), \
("string_type", ( [], [], ["type"]  ) ), \
("structure_type", ( [], [ "member", "access_declaration" ] + member_types, ["type", "with_named_children"]  ) ), \
("subroutine_type", ( [("type", False)], [], ["type"]  ) ), \
("typedef", ( [], [], ["type_chain"]  ) ), \
("union_type", ([], [ "member" ], ["type", "with_named_children"]  ) ), \
("unspecified_parameters", ( [], [], ["program_element"]  ) ), \
("variant", ( [], [] , ["basic"] ) ), \
("common_block", ( [], [], ["basic"]  ) ), \
("common_inclusion", ( [], [], ["basic"]  ) ), \
("inheritance", ( [("type", False), ("data_member_location", False)], [], ["basic"]  ) ), \
("inlined_subroutine", ( [("high_pc", False), ("low_pc", False), ("ranges", False)], [], ["with_runtime_location"]  ) ), \
("module", ( [], [], ["with_named_children"]  ) ), \
("ptr_to_member_type", ( [], [], ["type"]  ) ), \
("set_type", ( [], [], ["type"]  ) ), \
("subrange_type", ( [("type", True), ("upper_bound", False), ("lower_bound", False), ("count", False)], [], ["type"]  ) ), \
("with_stmt", ( [], [], ["basic"]  ) ), \
("access_declaration", ( [], [], ["basic"]  ) ), \
("base_type", ( [ ("encoding", True), ("bit_size", False), ("bit_offset", False)], [], ["type"] ) ), \
("catch_block", ( [], [], ["basic"]  ) ), \
("const_type", ( [], [], ["type_chain"]  ) ), \
("constant", ( [], [] , ["program_element"] ) ), \
("enumerator", ( [], [], ["basic"]  ) ), \
("file_type", ( [], [], ["type"]  ) ), \
("friend", ( [], [], ["basic"]  ) ), \
("namelist", ( [], [], ["basic"]  ) ), \
("namelist_item", ( [], [], ["basic"]  ) ), \
("packed_type", ( [], [], ["type_chain"]  ) ), \
("subprogram", ( [("type", False), ("calling_convention", False), ("low_pc", False), ("high_pc", False), ("frame_base", False)], [ "formal_parameter", "unspecified_parameters", "variable", "lexical_block" ], ["program_element", "with_runtime_location"]  ) ), \
("template_type_parameter", ( [], [], ["basic"]  ) ), \
("template_value_parameter", ( [], [], ["basic"]  ) ), \
("thrown_type", ( [], [], ["type_chain"]  ) ), \
("try_block", ( [], [], ["basic"]  ) ), \
("variant_part", ( [], [], ["basic"]  ) ), \
("variable", ( [ ("type", False), ("location", False) ], [] , ["program_element", "with_runtime_location", "with_stack_location"] ) ), \
("volatile_type", ( [], [], ["type_chain"]  ) ), \
("dwarf_procedure", ( [], [], ["basic"]  ) ), \
("restrict_type", ( [], [], ["type_chain"]  ) ), \
("interface_type", ( [], [ "member" ], ["type"]  ) ), \
("namespace", ( [], [], ["basic"]  ) ), \
("imported_module", ( [], [], ["basic"]  ) ), \
("unspecified_type", ( [], [] , ["type"] ) ), \
("partial_unit", ( [], [], ["basic"]  ) ), \
("imported_unit", ( [], [] , ["basic"] ) ), \
("condition", ( [], [], ["basic"]  ) ), \
("shared_type", ( [], [], ["type"]  ) ) \
]
tag_map = dict(tags)
