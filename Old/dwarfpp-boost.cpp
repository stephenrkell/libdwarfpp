#include "dwarfpp_simple.hpp"
#include <boost/python.hpp>

BOOST_PYTHON_MODULE(dwarfpp_boost)
{
    using namespace boost::python;
	class_<dwarf::file>("file", init<int>());
	
	class_<dwarf::encap::die>("die", init<dwarf::die&>());
	
	class_<dwarf::abi_information>("abi_information", init<dwarf::file&>())
        .def("get_funcs", &dwarf::abi_information::get_funcs, return_internal_reference<>())
        .def("get_toplevel_vars", &dwarf::abi_information::get_toplevel_vars, return_internal_reference<>())
		.def("get_types", &dwarf::abi_information::get_types, return_internal_reference<>())
    ;
	
	//class_<std::map<Dwarf_Off, dwarf::encap::die>>("off_die_map",
}
