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
("sibling", "refiter" ), \
("location", "loclist" ), \
("name", "string"), \
("ordering", "FIXME"), \
("byte_size", "unsigned"), \
("bit_offset", "unsigned" ), \
("data_bit_offset", "unsigned" ), \
("bit_size", "unsigned" ), \
("stmt_list", "FIXME" ), \
("low_pc", "address" ), \
("high_pc", "address" ), \
("language", "unsigned" ), \
("discr", "refiter" ), \
("discr_value", "signed" ), \
("visibility", "unsigned" ), \
("import", "refiter" ), \
("string_length", "loclist" ), \
("common_reference", "refiter" ), \
("comp_dir", "string" ), \
("containing_type", "refiter" ), \
("default_value", "refiter" ), \
("inline", "signed" ), \
("lower_bound", "unsigned" ), \
("producer", "string" ), \
("prototyped", "flag" ), \
("return_addr", "loclist" ), \
("start_scope", "FIXME" ), \
("bit_stride", "signed" ), \
("upper_bound", "unsigned" ), \
("abstract_origin", "refiter" ), \
("accessibility", "unsigned" ), \
("address_class", "unsigned" ), \
("artificial", "flag" ), \
("base_types", "refiter" ), \
("calling_convention", "unsigned" ), \
("const_value", "signed"), \
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
("friend", "refiter" ), \
("identifier_case", "unsigned" ), \
("is_optional", "flag"), \
("macro_info", "offset" ), \
("namelist_item", "refiter" ), \
("priority", "refiter" ), \
("segment", "loclist" ), \
("specification", "refiter" ), \
("static_link", "loclist" ), \
("type", "refiter_is_type" ), \
("use_location", "loclist" ), \
("variable_parameter", "flag" ), \
("virtuality", "unsigned" ), \
("vtable_elem_location", "loclist" ), \
("allocated", "refiter" ), \
("associated", "refiter" ), \
("data_location", "loclist" ), \
("byte_stride", "unsigned" ), \
("entry_pc", "address" ), \
("use_UTF8", "flag" ), \
("extension", "refiter" ), \
("ranges", "rangelist" ), \
("trampoline", "FIXME" ), \
("call_column", "unsigned" ), \
("call_file", "unsigned" ), \
("call_line", "unsigned" ), \
("description", "string" ), \
("binary_scale", "FIXME" ), \
("decimal_scale", "FIXME" ), \
("small", "refiter" ), \
("decimal_sign", "FIXME" ), \
("digit_count", "FIXME" ), \
("picture_string", "FIXME" ), \
("mutable", "flag" ), \
("threads_scaled", "flag" ), \
("explicit", "flag" ), \
("object_pointer", "refiter" ), \
("endianity", "unsigned" ), \
("elemental", "flag" ), \
("pure", "flag" ) \
] 
attr_type_map = dict(attr_types)

# artificial superclasses
# 
artificial_tags = [ \
("basic", ([], [], []) ),
("program_element", ([("name", False), ("decl_column", False ), ("decl_file", False), ("decl_line", False), ("prototyped", False), ("declaration", False), ("external", False), ("visibility", False), ("artificial", False)], [], ["basic"]) ), \
("with_instances", ([], [], ["program_element"])), \
("type", ([("byte_size", False )], [], ["with_instances"]) ), \
("type_describing_subprogram", ([("type", False)],  [], ["type"]) ), \
("type_chain", ([("type", False)],  [], ["type"]) ), \
("address_holding_type", ([("type", False)],  [], ["type_chain"]) ), \
("qualified_type", ([], [], ["type_chain"]) ), \
("with_named_children", ([], [], ["basic"])), \
("with_static_location", ([], [], ["basic"])), \
("with_dynamic_location", ([], [], ["with_type_describing_layout"])), \
("with_type_describing_layout", ([("type", False)], [], ["program_element"])), \
("with_data_members", ([], ["member"], ["type"]))
]
artificial_tag_map = dict(artificial_tags)

# At the moment we have the following abstractions of "things to do with memory layout":
# with_runtime_location: means having "static" location
# -- can be described by DW_AT_location, DW_AT_(MIPS_)?linkage_name, DW_AT_(high|low)_pc, 
# with_stack_location: means having a frame_base-relative (probably) location (BUT variables...)
# -- described by DW_AT_location dependent on a frame_base register
# with_type_describing_layout: means having a "type" attribute describing layout (so NOT subprogram)
# -- this means variables and formal parameters, whose layout is indirected
# with_data_members: means its layout is described directly by children
# -- there might be no DIE describing the object itself, e.g. heap objects
#
# We can separate out concepts as follows.
# Static/singleton versus dynamic/multiply-instantiated things
# -- e.g. variable (static/global) versus variable (local)
# The thing versus its description
# -- e.g. variable/fp versus type
# -- note that a "subprogram" DIE describes three things!
#    The static thing, a dynamic activation of the thing, and layout of the latter
# -- note that types themselves may contain "things" that have instance-relative locations
#    but types themselves do not have instance-relative locations
# 
# Candidate artificial tags:
# with_static_location (subprogram, variable [global], compile_unit)
# with_instances (subprogram, types)
# with_instance_relative_location (member, inheritance, fp, variable [local], )
#   ^-- these are things which, given the location of an instance of some enclosing DIE,
#       we can calculate a location for the things themselves. 
#       e.g. variables/fp: give frame base
#       e.g. members/inheritance: give object base
#   ^-- they are also almost-exactly the things that have types describing layout...
#       making them distinct from subprograms and compile_units
#       even though these things may also have *relative* locations (file-relative)
#       -- note that we keep a distinction from with_type_describing_layout...
#          ... e.g. for if DWARF ever splits global vars from local vars
# Maybe call these "with_dynamic_location"?
# And rename with_runtime_location to with_static_location?
# And unify with_stack_location?
# Is it always the immediate parent DIE that is the thing we pass an instance address of?
# -- maybe not with lexical_blocks, which enclose variables
# -- maybe we need an overridable get_dynamic_link() method...
#    ... that gets us the DIE that we need an instance address of?
# What would be a get_static_link()? HMM. Maybe just don't call it "link". get_instantiator()?
# YES, and this always returns a with_instances, i.e. a type or a subprogram!
# HMM. "instantiator" versus "with_instances" not quite right. 
# Why not? Well, it's not "containing object", more like "*class* of containing object"
# "instantiating_element"? "instantiating_definition"
#
# If we add DW_TAG_allocation_site,
# this will be an analogue of subprogram? i.e. a thing that has dynamic activations
# actually it will be more like a hypothetical DW_TAG_call_site
# cf. a hypothetical DW_TAG_allocation which would simply say that 
# some equivalence class of things may be allocated at some unspecified site(s)
# -- we might use this to record things like malloc(sizeof(X) + sizeof(Y)) --
#    the composite of X and Y is like a type, but never defined as a type

# abbreviations
member_types = [ "class_type", "typedef", "structure_type", "enumeration_type", "union_type" ]

# HACKs below:
# formal_parameter is listed with the is_optional, const_value and variable_parameter attrs
# but NOTE that these were added for the benefit of Cake, which 
# totally abuses them for its own purposes. In particular, const_value is inappropriate.

tags = [ \
("array_type", ( [], ["subrange_type"], ["type_chain"] ) ), \
("class_type", ( [], [ "member", "access_declaration" ] + member_types, ["with_data_members", "with_named_children"] ) ), \
("entry_point", ( [], [] , ["basic"] ) ), \
("enumeration_type", ( [("type", False)], ["enumerator"] , ["type", "with_named_children"] ) ), \
("formal_parameter", ( [("location", False), ("is_optional", False), ("variable_parameter", False), ("const_value", False)], [] , ["program_element", "with_dynamic_location"] ) ), \
("imported_declaration", ( [], [], ["basic"]  ) ), \
("label", ( [], [], ["basic"]  ) ), \
("lexical_block", ( [("low_pc", False), ("high_pc", False), ("ranges", False)], [ "variable" ] , ["with_static_location"] ) ), \
("member", ( [("data_member_location", False), ("bit_size", False), ("bit_offset", False), ("data_bit_offset", False)], [], ["program_element", "with_dynamic_location"]  ) ), \
("pointer_type", ( [("pure", False), ("address_class", False)], [], ["address_holding_type"]  ) ), \
("reference_type", ( [("address_class", False)], [], ["address_holding_type"]  ) ), \
("rvalue_reference_type", ( [("address_class", False)], [], ["address_holding_type"]  ) ), \
("compile_unit", ( [ ("language", True), ("comp_dir", False), ("producer", False), ("low_pc", False), ("high_pc", False), ("ranges", False), ("name", False), ("calling_convention", False)], [ "subprogram", "variable", "base_type", "pointer_type", "reference_type" ] + member_types, ["with_named_children", "with_static_location"]  ) ), \
("string_type", ( [ ("bit_size", False), ("string_length", False) ], [], ["type"]  ) ), \
("structure_type", ( [], [ "member", "access_declaration", "inheritance" ] + member_types, ["with_data_members", "with_named_children"]  ) ), \
("subroutine_type", ( [("calling_convention", False), ("pure", False), ("address_class", False)], ["formal_parameter", "unspecified_parameters"], ["type_describing_subprogram"]  ) ), \
("typedef", ( [], [], ["type_chain"]  ) ), \
("union_type", ([], [ "member" ], ["with_data_members", "with_named_children"]  ) ), \
("unspecified_parameters", ( [], [], ["program_element"]  ) ), \
("variant", ( [], [] , ["basic"] ) ), \
("common_block", ( [], [], ["basic"]  ) ), \
("common_inclusion", ( [], [], ["basic"]  ) ), \
("inheritance", ( [("data_member_location", False)], [], ["basic", "with_dynamic_location"]  ) ), \
("inlined_subroutine", ( [("high_pc", False), ("low_pc", False), ("ranges", False)], [], ["with_static_location"]  ) ), \
("module", ( [], [], ["with_named_children"]  ) ), \
("ptr_to_member_type", ( [], [], ["type"]  ) ), \
("set_type", ( [], [], ["type"]  ) ), \
("subrange_type", ( [("upper_bound", False), ("lower_bound", False), ("count", False), ("type", False)], [], ["type"]  ) ), \
("with_stmt", ( [], [], ["basic"]  ) ), \
("access_declaration", ( [], [], ["basic"]  ) ), \
("base_type", ( [ ("encoding", True), ("bit_size", False), ("bit_offset", False), ("data_bit_offset", False)], [], ["type"] ) ), \
("catch_block", ( [], [], ["basic"]  ) ), \
("const_type", ( [], [], ["qualified_type"]  ) ), \
("constant", ( [], [] , ["program_element"] ) ), \
("enumerator", ( [("const_value", False)], [], ["program_element"] ) ), \
("file_type", ( [], [], ["type"]  ) ), \
("friend", ( [], [], ["basic"]  ) ), \
("namelist", ( [], [], ["basic"]  ) ), \
("namelist_item", ( [], [], ["basic"]  ) ), \
("packed_type", ( [], [], ["qualified_type"]  ) ), \
("subprogram", ( [("calling_convention", False), ("low_pc", False), ("high_pc", False), ("frame_base", False), ("pure", False)], [ "formal_parameter", "unspecified_parameters", "variable", "lexical_block" ], ["type_describing_subprogram", "with_static_location", "with_named_children"]  ) ), \
("template_type_parameter", ( [], [], ["basic"]  ) ), \
("template_value_parameter", ( [], [], ["basic"]  ) ), \
("thrown_type", ( [], [], ["type_chain"]  ) ), \
("try_block", ( [], [], ["basic"]  ) ), \
("variant_part", ( [], [], ["basic"]  ) ), \
("variable", ( [("location", False)], [] , ["program_element", "with_static_location", "with_dynamic_location"] ) ), \
("volatile_type", ( [], [], ["qualified_type"]  ) ), \
("dwarf_procedure", ( [], [], ["basic"]  ) ), \
("restrict_type", ( [], [], ["qualified_type"]  ) ), \
("interface_type", ( [], [ "member" ], ["type"]  ) ), \
("namespace", ( [], [], ["program_element", "with_named_children"]  ) ), \
("imported_module", ( [], [], ["basic"]  ) ), \
("unspecified_type", ( [], [] , ["type"] ) ), \
("partial_unit", ( [], [], ["basic"]  ) ), \
("imported_unit", ( [], [] , ["basic"] ) ), \
("condition", ( [], [], ["basic"]  ) ), \
("shared_type", ( [], [], ["type"]  ) ) \
]
tag_map = dict(tags)
