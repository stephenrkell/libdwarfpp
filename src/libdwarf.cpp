#include "dwarfpp/libdwarf.hpp"
#include "dwarfpp/util.hpp"

namespace dwarf
{
	namespace core
	{
		using namespace dwarf::lib;

#ifndef NO_TLS
		__thread Dwarf_Error current_dwarf_error;
#else
		Dwarf_Error current_dwarf_error;
#endif
		
		void exception_error_handler(Dwarf_Error error, Dwarf_Ptr errarg)
		{
			throw Error(error, errarg); // FIXME: saner non-copying interface? 
		}
	}
	namespace lib
	{
		void default_error_handler(Dwarf_Error e, Dwarf_Ptr errarg) 
		{ 
			//fprintf(stderr, "DWARF error!\n"); /* this is the C version */
			/* NOTE: when called by a libdwarf function,
			 * errarg is always the Dwarf_Debug concerned with the error */
			core::debug() << "libdwarf error!" << std::endl;
			throw Error(e, errarg); /* Whoever catches this should also dwarf_dealloc the Dwarf_Error_s. */
		}
	} // end namespace lib
}
