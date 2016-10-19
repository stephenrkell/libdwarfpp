ACLOCAL_AMFLAGS = -I m4
AM_CXXFLAGS = -std=c++11 -ggdb3 -fvar-tracking-assignments -O2 -fkeep-inline-functions -Wall -Wno-deprecated-declarations -Iinclude -Iinclude/dwarfpp $(LIBSRK31CXX_CFLAGS) $(LIBCXXFILENO_CFLAGS)

extra_DIST = libdwarfpp.pc.in
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libdwarfpp.pc

dwarfpp_includedir = $(includedir)/dwarfpp
dwarfpp_include_HEADERS = include/dwarfpp/dwarf-onlystd.h include/dwarfpp/frame.hpp include/dwarfpp/attr.hpp include/dwarfpp/dwarf-onlystd-v2.h include/dwarfpp/lib.hpp include/dwarfpp/opt.hpp include/dwarfpp/dwarf3-adt.h include/dwarfpp/regs.hpp include/dwarfpp/dwarf3-factory.h include/dwarfpp/dwarf-ext-GNU.h include/dwarfpp/expr.hpp include/dwarfpp/spec.hpp

dwarfpp_private_includedir = $(includedir)/dwarfpp/private
dwarfpp_private_include_HEADERS = include/dwarfpp/private/libdwarf-handles.hpp include/dwarfpp/private/libdwarf.hpp

lib_LTLIBRARIES = src/libdwarfpp.la
src_libdwarfpp_la_SOURCES = src/expr.cpp src/lib.cpp src/attr.cpp src/frame.cpp src/regs.cpp src/spec.cpp
src_libdwarfpp_la_LIBADD = $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS) -lsupc++

INC_PP = include/dwarfpp
BUILT_SOURCES = $(INC_PP)/dwarf-onlystd.h $(INC_PP)/dwarf-onlystd-v2.h $(INC_PP)/dwarf-ext-GNU.h $(INC_PP)/dwarf3-adt.h $(INC_PP)/dwarf3-factory.h 
CLEANFILES = $(BUILT_SOURCES)

# launder the exit code of grep through "cat"
include/dwarfpp/dwarf-onlystd.h: $(libdwarf_includes)/dwarf.h
	cat "$<" | egrep -v 'DW_[A-Z]+_(GNU|SUN|HP|APPLE|INTEL|ARM|upc|PGI|ALTIUM|MIPS|CPQ|VMS|GNAT)' | \
		egrep -v '/\* (SGI|GNU)( \*/|\. )' | egrep -v 'LANG_Mips|LANG_Upc' | egrep -v '_use_GNAT' | egrep -v 'ATCF entries start at|DW_LANG_UPC instead.' | cat > "$@"

include/dwarfpp/dwarf-onlystd-v2.h: include/dwarfpp/dwarf-onlystd.h
	cat "$<" | grep -v 'DWARF[^2]' | cat > "$@"

include/dwarfpp/dwarf-ext-GNU.h: $(libdwarf_includes)/dwarf.h
	cat "$<" | egrep '(_|/\* |, )GNU' | egrep -vi conflict | egrep -vi '^[[:blank:]]*/\*' | cat > "$@"

include/dwarfpp/dwarf3-adt.h: spec/gen-adt-cpp.py spec/dwarf3.py
	python spec/gen-adt-cpp.py > "$@"

include/dwarfpp/dwarf3-factory.h: spec/gen-factory-cpp.py spec/dwarf3.py
	python spec/gen-factory-cpp.py > "$@"

lib/libdwarfpp.so: $(lib_LTLIBRARIES)
	mkdir -p lib && cd lib && ln -sf ../src/.libs/libdwarfpp.so .

lib/libdwarfpp.so.0: $(lib_LTLIBRARIES)
	mkdir -p lib && cd lib && ln -sf ../src/.libs/libdwarfpp.so.0 .

all: lib/libdwarfpp.so lib/libdwarfpp.so.0

# pkg-config doesn't understand PKG_CXXFLAGS, but I'm buggered
# if I'm going to have my Makefiles use _CFLAGS to mean _CXXFLAGS.
# So, if we find we have _CFLAGS set for these, either from pkg.m4
# or because pkg.m4 told the user to set this var, and if we *don't*
# have _CXXFLAGS for these things, propagate. Only one of the USER_*_CFLAGS
# and *_CFLAGS should be non-empty. Note that if we got this far,
# we *must* have a _CFLAGS, even if the user (sanely) didn't call it that,
# because pkg-config will have complained if we didn't fake it up.
if SUPPLIED_LIBCXXFILENO_CFLAGS
LIBCXXFILENO_CXXFLAGS = $(USER_LIBCXXFILENO_CFLAGS)
else
LIBCXXFILENO_CXXFLAGS = $(LIBCXXFILENO_CFLAGS)
endif
if SUPPLIED_LIBSRK31CXX_CFLAGS
LIBSRK31CXX_CXXFLAGS = $(USER_LIBSRK31CXX_CFLAGS)
else
LIBSRK31CXX_CXXFLAGS = $(LIBSRK31CXX_CFLAGS)
endif

export LIBCXXFILENO_CXXFLAGS LIBCXXFILENO_LIBS \
LIBSRK31CXX_CXXFLAGS LIBSRK31CXX_LIBS 
