/* libdwarf.i */
%module dwarf
%{
#include <dwarf.h>
#include <libdwarf.h>
#include <stdio.h>
#include <libelf.h>
%}

%include "typemaps.i"

/* typemap applications */

// this should match only dwarf_next_cu_header
%apply SWIGTYPE *OUTPUT { Dwarf_Unsigned *cu_header_length, Dwarf_Half *version_stamp,
Dwarf_Unsigned *abbrev_offset, Dwarf_Half *address_size, Dwarf_Unsigned *next_cu_header };

        
%include <dwarf.h>
%include "libdwarf.h-hacked" // version with declarations of undefined functions sedded out
        
%inline %{
Dwarf_Debug staticDwarfDebug;
Dwarf_Debug *staticDwarfDebugPtr = &staticDwarfDebug;

void defaultDwarfHandler(Dwarf_Error error, Dwarf_Ptr errarg) { fprintf(stderr, "DWARF error!\n"); }
Dwarf_Handler staticDwarfHandler = &defaultDwarfHandler;
Dwarf_Handler *staticDwarfHandlerPtr = &staticDwarfHandler;

Dwarf_Error staticDwarfError;
Dwarf_Error *staticDwarfErrorPtr = &staticDwarfError;

int dwarf_elf_end(Elf *elf) { return elf_end(elf); }

/* Fake definitions for undefined structs -- needed to make Swig 
   generate Python class definitions, rather than complaining. */
//struct Dwarf_Debug_fake { Dwarf_Debug p; };

%} // end inline

/* Extensions. */


%extend Dwarf_Debug_s {
    Dwarf_Debug_s(int fd, Dwarf_Unsigned access,
        Dwarf_Handler errhand,
        Dwarf_Ptr errarg,
        Dwarf_Error *error) {
        
        Dwarf_Debug dbg; // handle on a Dwarf_Debug object        
        int retval;
        retval = dwarf_init(fd, access, errhand, errarg, &dbg, error);
        if (retval == DW_DLV_OK) return dbg;
        else return 0; // FIXME: else what? we want to throw an exception
    }
    
    Elf *getElf(Dwarf_Error *error = staticDwarfErrorPtr)
    {
		Elf *dw_elf;
        int retval;
        retval = dwarf_get_elf(self, &dw_elf, error);
        if (retval == DW_DLV_OK) return dw_elf;
        else return 0; // FIXME: else what? we want to throw an exception
    }
    
    void finish(Dwarf_Error *error = staticDwarfErrorPtr)
    {
        int retval;
        retval = dwarf_finish(self,error);
        if (retval == DW_DLV_OK) return;
        else return; // FIXME: else what? we want to throw an exception
    }
    
	void nextCUHeader(Dwarf_Unsigned *cu_header_length, Dwarf_Half *version_stamp,
		Dwarf_Unsigned *abbrev_offset, Dwarf_Half *address_size, Dwarf_Unsigned *next_cu_header,
        Dwarf_Error *error = staticDwarfErrorPtr)
    {
    	int retval;
        retval = dwarf_next_cu_header(self, cu_header_length, version_stamp,
        	abbrev_offset, address_size, next_cu_header, error);
        if (retval == DW_DLV_OK) return;
        else return; // FIXME: else what? we want to throw an exception
    }
    
    /* TODO: destruction logic? */
};
