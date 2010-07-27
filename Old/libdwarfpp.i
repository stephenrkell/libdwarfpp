/* libdwarfpp.i */
%module dwarfpp
%{
#include "dwarfpp_simple.hpp"
#include <stdio.h>
#include <libelf.h>
%}

//%include "typemaps.i"
%include "cpointer.i"
%include "std_map.i"

/* typemap applications */

/* renames and template instantiations */
%rename(encap_die) dwarf::encap::die;
//%template(die_map) std::map<Dwarf_Off,dwarf::encap::die>;

extern "C" {     
%include <dwarf.h>
}

%include "dwarfpp.h"
//class std::map<Dwarf_Off, dwarf::encap::die>; // explicit template instantiation
%include "dwarfpp_simple.hpp" // version with declarations of undefined functions sedded out
//%pointer_class(std::map<Dwarf_Off, dwarf::encap::die>);
