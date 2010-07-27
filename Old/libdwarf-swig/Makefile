INCLUDE_ARGS := -I${HOME}/opt/include -I/usr/include/python2.5
CFLAGS := -g $(INCLUDE_ARGS)
LDFLAGS := -L${HOME}/opt/lib

default: _dwarf.so

libdwarf_wrap.c: swig_outputs

dwarf.py: swig_outputs

# libdwarf.h declares some functions (prototypes) that are never
# subsequently defined in any source file. We don't want swig to
# try to wrap these, since it will obviously cause some link errors,
# so hack the include file to remove mention of them.
libdwarf.h-hacked: ${HOME}/opt/include/libdwarf.h Makefile
	cat ${HOME}/opt/include/libdwarf.h | tr '\n' '\f' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_frame_get_reg_register([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_frame_get_reg_expression([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_pcfile([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_pcsubr([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_pcscope([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_stringlen([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_subscrcnt([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_nthsubscr([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_isbitfield([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_dieline([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_pclines([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_seterrhand([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_seterrarg([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_pubtype_die_offset([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_get_macro([^)]*);//' | \
		sed 's/[0-9a-zA-Z_]\+ *dwarf_get_all_defined_macros([^)]*);//' | \
		tr '\f' '\n' > "$@"

.PHONY: swig_outputs
swig_outputs: libdwarf.i libdwarf.h-hacked
	swig $(INCLUDE_ARGS) -python "$<"

libdwarf_wrap.o: libdwarf_wrap.c
	cc $(CFLAGS) -c -o "$@" "$<"

# We're assuming libdwarf is available as an archive (.a) file.
# This complicates the link: we want to generate a shared lib
# that includes the whole of this .a file (since this is the
# only way the Python interpreter will be able to load the 
# libdwarf code), but is dynamically linked against libelf
# (this dependency will be handled by ldd in the usual way,
# since libelf is available as a shared library).
_dwarf.so: swig_outputs libdwarf_wrap.o
	ld $(LDFLAGS) --whole-archive -shared -lelf -static -ldwarf libdwarf_wrap.o -o _dwarf.so 
