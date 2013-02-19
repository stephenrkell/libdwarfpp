#ifndef DWARFPP_LIBDWARF_HPP_
#define DWARFPP_LIBDWARF_HPP_

#include <libelf.h>

namespace dwarf
{
	namespace lib
	{
		//using namespace ::dwarf::spec;
		extern "C"
		{
			// HACK: libdwarf.h declares struct Elf opaquely, and we don't
			// want it in the dwarf::lib namespace, so preprocess this.
			#define Elf Elf_opaque_in_libdwarf
			#include <libdwarf.h>
			#undef Elf
		}
		// forward decls
		struct No_entry;
		struct evaluator;
		struct loclist;
		
		// we need this soon
		/* The point of this class is to make a self-contained throwable object
		 * out of a Dwarf_Error handle. FIXME: why do we bundle the Ptr but not the
		 * error function? */
		struct Error {
			Dwarf_Error e;
			Dwarf_Ptr arg;
			Error(Dwarf_Error e, Dwarf_Ptr arg) : e(e), arg(arg) {}
			virtual ~Error() 
			{ /*dwarf_dealloc((Dwarf_Debug) arg, e, DW_DLA_ERROR); */ /* TODO: Fix segfault here */	}
		};
	}
}

#endif
