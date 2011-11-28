#define declare_base(base) virtual base ## _die

#define base_fragment(base) base ## _die(ds, p_d) {}

#define initialize_base(fragment) fragment ## _die(ds, p_d)

#define constructor(fragment, ...) \
	fragment ## _die(dieset& ds, boost::shared_ptr<lib::die> p_d) : \
        __VA_ARGS__ {}

#define begin_class(fragment, base_inits, ...) \
	struct fragment ## _die : __VA_ARGS__ { \n\
    	constructor(fragment, base_inits)

#define base_initializations(...) __VA_ARGS__

#define end_class(fragment) \
	};

#define stored_type_string std::string
#define stored_type_flag bool
#define stored_type_unsigned Dwarf_Unsigned
#define stored_type_signed Dwarf_Signed
#define stored_type_base base&
#define stored_type_offset Dwarf_Off
#define stored_type_half Dwarf_Half
#define stored_type_ref Dwarf_Off
#define stored_type_tag Dwarf_Half
#define stored_type_loclist dwarf::encap::loclist
#define stored_type_address Dwarf_Addr
#define stored_type_refdie boost::shared_ptr<spec::basic_die> 
#define stored_type_refdie_is_type boost::shared_ptr<spec::type_die> 

#define attr_optional(name, stored_t) \
	virtual boost::optional<stored_type_ ## stored_t> get_ ## name() const = 0; \n\
  	boost::optional<stored_type_ ## stored_t> name() const { return get_ ## name(); }

#define attr_mandatory(name, stored_t) \
	virtual stored_type_ ## stored_t get_ ## name() const = 0; \n\
  	stored_type_ ## stored_t name() const { return get_ ## name(); } 



// compile_unit_die has an override for get_next_sibling()
#define extra_decls_compile_unit \
		boost::shared_ptr<spec::basic_die> get_next_sibling(); 

#include "dwarf3-adt.h"
