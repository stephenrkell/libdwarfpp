DWARF_PREFIX ?= /usr
#DWARF_PREFIX ?= $(HOME)/opt
INCLUDE_ARGS := -I$(DWARF_PREFIX)/include -I/usr/include/python2.5 -I/usr/include -I../../libsrk31c++
CFLAGS := -Wall -O0 -g3 $(INCLUDE_ARGS)
LDFLAGS := -L${HOME}/opt/lib -L../../libsrk31c++
CXXFLAGS := $(CFLAGS)
comma = ,
CXX_LDFLAGS := $(patsubst -Wl$(comma)-%,-%,$(LDFLAGS))
HDRS := $(wildcard *.hpp)

default: dwarfpp.o libdwarfpp.a dwarfpp_test #_dwarfpp.so 

libdwarfpp.a: dwarfpp.o dwarfpp_simple.o dwarfpp_util.o
	ar r "$@" $^

test: dwarfpp_test
#	./dwarfpp_test test-object.o
	./dwarfpp_test self-test-object
	
dwarfpp_test: dwarfpp_test.cpp libdwarfpp.a
	g++ $(CFLAGS) \
	$(CXX_LDFLAGS) \
	-L. \
	-o "$@" "$<" -ldwarfpp -ldwarf -lelf -lsrk31c++
# -ldwarfpp -Wl,-lelf,--whole-archive,-ldwarf,--no-whole-archive \

dwarfpp.o: dwarfpp.cpp dwarfpp.h $(HDRS)
	g++ $(CFLAGS) -c -o "$@" dwarfpp.cpp

dwarfpp_simple.o: dwarfpp_simple.cpp dwarfpp_simple.hpp dwarfpp.o $(HDRS)
	g++ $(CFLAGS) -c -o "$@" dwarfpp_simple.cpp

dwarfpp_util.o: dwarfpp_util.cpp dwarfpp_util.hpp $(HDRS)
	g++ $(CFLAGS) -c -o "$@" dwarfpp_util.cpp

libdwarfpp_wrap.cxx: swig_outputs

dwarf.py: swig_outputs

# libdwarf.h declares some functions (prototypes) that are never
# subsequently defined in any source file. We don't want swig to
# try to wrap these, since it will obviously cause some link errors,
# so hack the include file to remove mention of them.
libdwarf.h-hacked: $(DWARF_PREFIX)/include/libdwarf.h Makefile
	cat "$<" | tr '\n' '\f' | \
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
swig_outputs: libdwarfpp.i libdwarf.h-hacked
	swig -shadow -c++ $(INCLUDE_ARGS) -python "$<"

libdwarfpp_wrap.o: libdwarfpp_wrap.cxx
	cc $(CFLAGS) -c -o "$@" "$<"

# We're assuming libdwarf is available as an archive (.a) file.
# This complicates the link: we want to generate a shared lib
# that includes the whole of this .a file (since this is the
# only way the Python interpreter will be able to load the 
# libdwarf code), but is dynamically linked against libelf
# (this dependency will be handled by ldd in the usual way,
# since libelf is available as a shared library).
_dwarfpp.so: swig_outputs libdwarfpp_wrap.o dwarfpp.o dwarfpp_simple.o
	#ld $(LDFLAGS) -L/usr/lib -shared -lelf -lstdc++ -static --whole-archive -ldwarf --no-whole-archive libdwarfpp_wrap.o -o _dwarfpp.so 
	g++ $(CXX_LDFLAGS) -Wl,-lelf,-lstdc++,--whole-archive,-ldwarf,--no-whole-archive dwarfpp.o dwarfpp_simple.o libdwarfpp_wrap.o -Wl,-shared -o "$@"

dwarfpp_boost.so: dwarfpp-boost.o dwarfpp.o dwarfpp_simple.o
	g++ $(CXX_LDFLAGS) -Wl,-lelf,-lstdc++,--whole-archive,-ldwarf,--no-whole-archive,-lboost_python dwarfpp.o dwarfpp_simple.o dwarfpp-boost.o -Wl,-shared -o "$@"
