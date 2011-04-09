#!/usr/bin/env python

# What needs to be generated?
# - abstract class template definitions
# - forward declarations thereof
# - encap concrete class declarations
# - forward declarations thereof
# - encap concrete class 
# - encap factory method

# the complete set of DWARF 3 (rev. f onwards) tags,
# with our extra schematic info as follows.
# what attributes each has: name, optional or mandatory
# what of children it (typically) has -- we have to supply this
# *** a separate table has the encap libdwarf rep of each attribute

import sys

stored_types = [ \
("string", "std::string"), \
("flag", "bool"), \
("unsigned", "Dwarf_Unsigned"), \
("signed", "Dwarf_Signed"), \
("base", "base&"), \
("offset", "Dwarf_Off"), \
("address", "dwarf::encap::attribute_value::address"), \
("half", "Dwarf_Half"), \
("ref", "Dwarf_Off"), \
("refdie", "boost::shared_ptr< dwarf::spec::basic_die >"), \
("refdie_is_type", "boost::shared_ptr< dwarf::spec::type_die >"), \
("loclist", "dwarf::encap::loclist"), \
("rangelist", "dwarf::encap::rangelist"), \
("FIXME", "dwarf::encap::loclist") \
]

stored_type_map = dict(stored_types)
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

decl_attrs = [ ("decl_column", False ), ("decl_file", False), ("decl_line", False), ("name", False) ]
member_types = [ "class_type", "typedef", "structure_type", "enumeration_type", "union_type" ]
prog_el_attrs = decl_attrs + [ ("prototyped", False), ("declaration", False), ("external", False), ("visibility", False) ]
type_attrs = prog_el_attrs + [ ("byte_size", False) ]
type_chain_attrs = type_attrs + [("type", False)]

dwarf3_tags = [ \
("array_type", ( type_attrs + [("type", False)], ["subrange_type"], ["is_type"] ) ), \
("class_type", ( type_attrs + [], [ "member", "access_declaration" ] + member_types, ["is_type", "has_named_children"] ) ), \
("entry_point", ( [], [] , ["base"] ) ), \
("enumeration_type", ( type_attrs + [("type", False)], ["enumerator"] , ["is_type", "has_named_children"] ) ), \
("formal_parameter", ( prog_el_attrs + [("type", False), ("location", False) ], [] , ["is_program_element"] ) ), \
("imported_declaration", ( decl_attrs + [], [], ["base"]  ) ), \
("label", ( decl_attrs + [], [], ["base"]  ) ), \
("lexical_block", ( [("low_pc", False), ("high_pc", False), ("ranges", False)], [ "variable" ] , ["base"] ) ), \
("member", ( prog_el_attrs + [("type", False), ("data_member_location", False)], [], ["is_program_element"]  ) ), \
("pointer_type", ( type_chain_attrs + [], [], ["is_type_chain"]  ) ), \
("reference_type", ( type_chain_attrs + [], [], ["is_type_chain"]  ) ), \
("compile_unit", ( [ ("language", True), ("comp_dir", False), ("low_pc", False), ("high_pc", False), ("ranges", False), ("name", False)], [ "subprogram", "variable", "base_type", "pointer_type", "reference_type" ] + member_types, ["has_named_children"]  ) ), \
("string_type", ( type_attrs + [], [], ["is_type"]  ) ), \
("structure_type", ( prog_el_attrs + [("byte_size", False)], [ "member", "access_declaration" ] + member_types, ["is_type", "has_named_children"]  ) ), \
("subroutine_type", ( type_attrs + [("type", False)], [], ["is_type"]  ) ), \
("typedef", ( type_chain_attrs + [], [], ["is_type_chain"]  ) ), \
("union_type", ( type_attrs + [], [ "member" ], ["is_type", "has_named_children"]  ) ), \
("unspecified_parameters", ( prog_el_attrs + [], [], ["is_program_element"]  ) ), \
("variant", ( decl_attrs + [], [] , ["base"] ) ), \
("common_block", ( decl_attrs + [], [], ["base"]  ) ), \
("common_inclusion", ( decl_attrs + [], [], ["base"]  ) ), \
("inheritance", ( prog_el_attrs + [("type", False), ("data_member_location", False)], [], ["is_program_element"]  ) ), \
("inlined_subroutine", ( [("high_pc", False), ("low_pc", False), ("ranges", False)], [], ["base"]  ) ), \
("module", ( decl_attrs + [], [], ["has_named_children"]  ) ), \
("ptr_to_member_type", ( type_attrs + [], [], ["is_type"]  ) ), \
("set_type", ( type_attrs + [], [], ["is_type"]  ) ), \
("subrange_type", ( type_attrs + [("type", True), ("upper_bound", False), ("lower_bound", False), ("count", False)], [], ["is_type"]  ) ), \
("with_stmt", ( [], [], ["base"]  ) ), \
("access_declaration", ( decl_attrs + [], [], ["base"]  ) ), \
("base_type", ( type_attrs + [ ("encoding", True), ("bit_size", False), ("bit_offset", False)], [], ["is_type"] ) ), \
("catch_block", ( [], [], ["base"]  ) ), \
("const_type", ( type_chain_attrs + [], [], ["is_type_chain"]  ) ), \
("constant", ( prog_el_attrs + [], [] , ["is_program_element"] ) ), \
("enumerator", ( decl_attrs + [], [], ["base"]  ) ), \
("file_type", ( type_attrs + [], [], ["is_type"]  ) ), \
("friend", ( decl_attrs + [], [], ["base"]  ) ), \
("namelist", ( decl_attrs + [], [], ["base"]  ) ), \
("namelist_item", ( decl_attrs + [], [], ["base"]  ) ), \
("packed_type", ( type_chain_attrs + [], [], ["is_type_chain"]  ) ), \
("subprogram", ( prog_el_attrs + [("type", False), ("calling_convention", False), ("low_pc", False), ("high_pc", False), ("ranges", False), ("frame_base", False)], [ "formal_parameter", "unspecified_parameters", "variable", "lexical_block" ], ["is_program_element", "has_named_children"]  ) ), \
("template_type_parameter", ( decl_attrs + [], [], ["base"]  ) ), \
("template_value_parameter", ( decl_attrs + [], [], ["base"]  ) ), \
("thrown_type", ( type_chain_attrs + [], [], ["is_type_chain"]  ) ), \
("try_block", ( [], [], ["base"]  ) ), \
("variant_part", ( decl_attrs + [], [], ["base"]  ) ), \
("variable", ( prog_el_attrs + [ ("type", False), ("location", False) ], [] , ["is_program_element"] ) ), \
("volatile_type", ( type_chain_attrs + [], [], ["is_type_chain"]  ) ), \
("dwarf_procedure", ( [], [], ["base"]  ) ), \
("restrict_type", ( type_chain_attrs + [], [], ["is_type_chain"]  ) ), \
("interface_type", ( type_attrs + [], [ "member" ], ["is_type"]  ) ), \
("namespace", ( decl_attrs + [], [], ["base"]  ) ), \
("imported_module", ( decl_attrs + [], [], ["base"]  ) ), \
("unspecified_type", ( type_attrs + [], [] , ["is_type"] ) ), \
("partial_unit", ( [], [], ["base"]  ) ), \
("imported_unit", ( [], [] , ["base"] ) ), \
("condition", ( decl_attrs + [], [], ["base"]  ) ), \
("shared_type", ( type_attrs + [], [], ["is_type"]  ) ) \
]

def pluralise(s):
    if (s.endswith("s")):
        return s
    else:
        return "".join([s, "s"])

def output_forward_decls(out, prefix, hdr = "", tmpl_suffix = ""):
    for tag in dwarf3_tags:
        out.write("%s class %s_%s%s;\n" % (hdr, prefix, tag[0], ""))
        
def output_class_decls(out, prefix, hdr, inheritance_tmpl, self_typedef_prefix, tmpl_suffix, \
    pure_methods, constructor_proto_args = "", constructor_call_args = ""):
    if (pure_methods):
        virtual_insert = " = 0"
    else:
        virtual_insert = ""
    for tag in dwarf3_tags:
        if (hdr == ""):
            abstract_base_insert = "public virtual abstract::Die_abstract_" + tag[0] + "<Rep>, " \
                + "public virtual spec::" + tag[0] + "_die, public " + prefix + "_base" #"%s" % tag[1][2]
        else:
            abstract_base_insert = "public virtual " + ", public virtual ".join([inheritance_tmpl % base_class for base_class in tag[1][2]])
        #sys.stderr.write("tag is %s\n" % str(tag))
        out.write(hdr + "\n")
        out.write("class %s_%s : %s {\n" \
            % (prefix, tag[0], abstract_base_insert))
        out.write("public:\n")
        out.write("typedef %s_%s %sself;\n" % (prefix, tag[0], self_typedef_prefix))
        if (not pure_methods):
            # emit constructors
            for i in range(0, len(constructor_proto_args)):
                out.write("%s_%s(%s) : " % (prefix, tag[0], constructor_proto_args[i]))
                out.write("%s_base(DW_TAG_%s, %s)" % (prefix, tag[0], constructor_call_args[i]))
                out.write("{}\n")
        for attr in tag[1][0]: 
            #sys.stderr.write("attr is %s\n" % str(attr))
            if not attr[1]: # optional
                out.write("virtual boost::optional<%s> get_%s() const%s;\n" \
                    % (stored_type_map[attr_type_map[attr[0]]], attr[0], virtual_insert))
                out.write("boost::optional<%s> %s() const { return get_%s(); }\n" \
                    % (stored_type_map[attr_type_map[attr[0]]], attr[0], attr[0]))
                out.write("virtual %sself& set_%s(boost::optional<%s> arg)%s;\n" \
                    % (self_typedef_prefix, attr[0], \
                    stored_type_map[attr_type_map[attr[0]]], virtual_insert))
            else: # mandatory
                out.write("virtual %s get_%s() const%s;\n" \
                    % (stored_type_map[attr_type_map[attr[0]]], attr[0], virtual_insert))
                out.write("%s %s() const { return get_%s(); }\n" \
                    % (stored_type_map[attr_type_map[attr[0]]], attr[0], attr[0]))
                out.write("virtual %sself& set_%s(%s arg)%s;\n" \
                    % (self_typedef_prefix, attr[0], \
                        stored_type_map[attr_type_map[attr[0]]], virtual_insert))
        if (hdr == ""):
            typename_infix = ""
        else:
            typename_infix = "typename"
        typename_infix
        for child in tag[1][1]:
            #sys.stderr.write("child is %s\n" % str(child))
            out.write("virtual %s abstract::iters<Rep, DW_TAG_%s>::iterator %s_begin()%s;\n" \
                % (typename_infix, child, pluralise(child), virtual_insert))
            out.write("virtual %s abstract::iters<Rep, DW_TAG_%s>::iterator %s_end()%s;\n" \
                % (typename_infix, child, pluralise(child), virtual_insert))
        out.write("#ifdef %s_EXTRA_FUNCTION_DECLS\n\t%s_EXTRA_FUNCTION_DECLS\n#endif\n" \
            % (tag[0], tag[0]))
        out.write("};\n")
        
def output_tag_specialisations(out, prefix, hdr, tmpl_suffix, ns):
    out.write("} namespace abstract {\n")
    for tag in dwarf3_tags:
        if (hdr == ""):
            template_hdr = "template <> "
            ns_prefix = "%s::" % ns
        else:
            template_hdr = hdr
            ns_prefix = ""
        out.write(template_hdr + "struct tag<%sRep, DW_TAG_%s> { " % (ns_prefix, tag[0]) \
            + "typedef %s::%s_%s%s type; };\n" % (ns, prefix, tag[0], tmpl_suffix))
    out.write("} namespace %s {\n" % ns)

def output_method_defns(out, prefix):
    for tag in dwarf3_tags:
        #sys.stderr.write("tag is %s\n" % str(tag))
        for attr in tag[1][0]: 
            #sys.stderr.write("attr is %s\n" % str(attr))
            if not attr[1]: # optional
                out.write("boost::optional<%s> %s_%s::get_%s() const\n" \
                    % (stored_type_map[attr_type_map[attr[0]]], prefix, tag[0], attr[0]))
                if attr_type_map[attr[0]].startswith("refdie"):
                    target_type = stored_type_map[attr_type_map[attr[0]]] + "::element_type"
                    out.write("{ if (has_attr(DW_AT_%s)) return boost::dynamic_pointer_cast<%s>((*this)[DW_AT_%s].get_%s ());\n" \
                        % (attr[0], target_type, \
                        attr[0], attr_type_map[attr[0]].split('_')[0])) # refdie_is_type becomes refdie
                else:
                    out.write("{ if (has_attr(DW_AT_%s)) return (*this)[DW_AT_%s].get_%s ();\n" \
                        % (attr[0], attr[0], attr_type_map[attr[0]])) 
                out.write("else return boost::optional<%s>(); }\n" % \
                    stored_type_map[attr_type_map[attr[0]]])
                out.write("%s_%s& %s_%s::set_%s(boost::optional<%s> arg)\n" \
                    % (prefix, tag[0], prefix, tag[0], attr[0], \
                    stored_type_map[attr_type_map[attr[0]]]))
                if attr_type_map[attr[0]].startswith("refdie"):
                    out.write("{ if (arg) put_attr(DW_AT_%s, boost::dynamic_pointer_cast<Die_encap_base>(*arg));\n" \
                        % (attr[0]))
                else:
                    out.write("{ if (arg) put_attr(DW_AT_%s, encap::attribute_value(this->m_ds,*arg));\n" \
                        % (attr[0]))
                out.write("    else m_attrs.erase(DW_AT_%s); return *this; }\n" % attr[0])
            else: # mandatory
                out.write("%s %s_%s::get_%s() const\n" \
                    % (stored_type_map[attr_type_map[attr[0]]], prefix, tag[0], attr[0]))
                if attr_type_map[attr[0]].startswith("refdie"):
                    target_type = stored_type_map[attr_type_map[attr[0]]] + "::element_type"
                    out.write("{ return boost::dynamic_pointer_cast<%s>((*this)[DW_AT_%s].get_%s ()); }\n" \
                        % ( target_type, \
                        attr[0], attr_type_map[attr[0]].split('_')[0])) # refdie_is_type becomes refdie
                else:
                    out.write("{ return (*this)[DW_AT_%s].get_%s (); }\n" \
                        % ( attr[0], attr_type_map[attr[0]]))
                out.write("%s_%s& %s_%s::set_%s(%s arg)\n" \
                    % (prefix, tag[0], prefix, tag[0], attr[0], \
                        stored_type_map[attr_type_map[attr[0]]]))
                if attr_type_map[attr[0]].startswith("refdie"):
                    out.write("{ put_attr(DW_AT_%s, boost::dynamic_pointer_cast<Die_encap_base>(arg)); return *this; }\n" \
                        % (attr[0]))
                else:
                    out.write("{ put_attr(DW_AT_%s, encap::attribute_value(this->m_ds, arg)); return *this; }\n" \
                        % (attr[0]))
                
        for child in tag[1][1]:
            #sys.stderr.write("child is %s\n" % str(child))
            out.write("abstract::iters<Rep, DW_TAG_%s>::iterator %s_%s::%s_begin()\n" \
                % (child, prefix, tag[0], pluralise(child)))
            out.write("{ return abstract::iters<Rep, DW_TAG_%s>::base_iterator(children_begin(), children_end()); }\n" \
                % child);
            out.write("abstract::iters<Rep, DW_TAG_%s>::iterator %s_%s::%s_end()\n" \
                % (child, prefix, tag[0], pluralise(child)))
            out.write("{ return abstract::iters<Rep, DW_TAG_%s>::base_iterator(children_end(), children_end()); }\n" \
                % child);

def output_iterator_typedefs(out, prefix, rep):
    for tag in dwarf3_tags:
        out.write("typedef abstract::iters<%s, DW_TAG_%s>::base_iterator %s_base_iterator;\n" \
            % (rep, tag[0], pluralise(tag[0])))
        out.write("typedef abstract::iters<%s, DW_TAG_%s>::iterator %s_iterator;\n" \
            % (rep, tag[0], pluralise(tag[0])))
            
def output_factory_cases(out, prefix, ns, constructor_call_args):
    for tag in dwarf3_tags:
        out.write("case DW_TAG_%s: return boost::make_shared<%s::%s_%s>(%s);" \
            % (tag[0], ns, prefix, tag[0], constructor_call_args))

def output_adt_interfaces(out):
    for tag in dwarf3_tags:
        out.write("struct %s_die : " % tag[0])
        bases_written = 0
        for base in tag[1][2]:
            out.write("\n\tpublic virtual %s_die" % base)
            bases_written = bases_written + 1
            if bases_written < len(tag[1][2]):
                out.write(", ")
        out.write("\n{\n")
        for attr in tag[1][0]:
            out.write("\tvirtual AT_%s get_%s() const = 0;\n" % (attr_type_map[attr[0]], attr[0]))
        out.write("#ifdef %s_EXTRA_DECLS\n\t%s_EXTRA_DECLS\n#endif\n" \
            % (tag[0], tag[0]))
        out.write("\n};\n")
            
def main(argv):
    abstract_preamble_gen = open("include/dwarfpp/abstract_preamble_gen.inc", "w+")
    output_forward_decls(abstract_preamble_gen, "Die_abstract", "template <class Rep>", "<Rep>")
    output_tag_specialisations(abstract_preamble_gen, "Die_abstract", "template <class Rep>", "<Rep>", "abstract")

    abstract_hdr_gen = open("include/dwarfpp/abstract_hdr_gen.inc", "w+")
    output_class_decls(abstract_hdr_gen, "Die_abstract", "template <class Rep> ", \
        "Die_abstract_%s<Rep>", "abstract_", "<Rep>", True)

    encap_preamble_gen = open("include/dwarfpp/encap_preamble_gen.inc", "w+")
    output_forward_decls(encap_preamble_gen, "Die_encap")
    output_tag_specialisations(encap_preamble_gen, "Die_encap", "", "", "encap")

    encap_hdr_gen = open("include/dwarfpp/encap_hdr_gen.inc", "w+")
    output_class_decls(encap_hdr_gen, "Die_encap", "", \
        "Die_abstract_%s<Rep>", "", "", False, \
        ["Die_encap_base& parent, boost::optional<const std::string&> name", \
            "dwarf::encap::dieset& ds, dwarf::lib::die& d, Dwarf_Off parent_off" ], \
        ["parent, name", \
            "ds, d, parent_off"] )

    encap_src_gen = open("src/encap_src_gen.inc", "w+")
    output_method_defns(encap_src_gen, "Die_encap")

    encap_factory_gen = open("src/encap_factory_gen.inc", "w+")
    output_factory_cases(encap_factory_gen, "Die_encap", "dwarf::encap", "ds, d, parent_off")
    
    encap_hdr_typedefs_gen = open("include/dwarfpp/encap_typedefs_gen.inc", "w+")
    output_iterator_typedefs(encap_hdr_typedefs_gen, "Die_encap", "die")

    adt_interfaces = open("include/dwarfpp/adt_interfaces.inc", "w+")
    output_adt_interfaces(adt_interfaces)
    
# main script
if __name__ == "__main__":
    main(sys.argv[1:])
