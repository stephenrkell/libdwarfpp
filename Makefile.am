ACLOCAL_AMFLAGS = -I m4
AM_CXXFLAGS = -std=c++14 -fvar-tracking-assignments -O1 -g -Wall -Wno-deprecated-declarations -Iinclude -Iinclude/dwarfpp $(LIBSRK31CXX_CFLAGS) $(LIBCXXFILENO_CFLAGS)

extra_DIST = libdwarfpp.pc.in
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libdwarfpp.pc

dwarfpp_includedir = $(includedir)/dwarfpp
# Installed (public) headers. The choice of DWARF backend is hidden behind the
# two switching headers (handles.hpp, dwarfbackend.hpp); consumers include those
# (directly or via lib.hpp) and never the backend-specific headers, which are
# private to the library (see noinst_HEADERS below).
dwarfpp_include_HEADERS = include/dwarfpp/dwarf-onlystd.h include/dwarfpp/frame.hpp \
  include/dwarfpp/attr.hpp include/dwarfpp/dwarf-onlystd-v2.h include/dwarfpp/lib.hpp \
  include/dwarfpp/opt.hpp include/dwarfpp/dwarf-current-adt.h include/dwarfpp/regs.hpp \
  include/dwarfpp/dwarf-current-factory.h include/dwarfpp/dwarf-ext-GNU.h \
  include/dwarfpp/expr.hpp include/dwarfpp/spec.hpp \
  include/dwarfpp/util.hpp \
  include/dwarfpp/abstract.hpp \
  include/dwarfpp/root.hpp \
  include/dwarfpp/iter.hpp \
  include/dwarfpp/dies.hpp \
  include/dwarfpp/root-inl.hpp \
  include/dwarfpp/abstract-inl.hpp \
  include/dwarfpp/iter-inl.hpp \
  include/dwarfpp/dies-inl.hpp \
  include/dwarfpp/handles.hpp include/dwarfpp/dwarfbackend.hpp \
  include/dwarfpp/config.h

# Backend-specific headers: private to the library, not installed. They are
# selected by the switching headers above according to DWARFPP_USE_LIBDW.
noinst_HEADERS = \
  include/dwarfpp/libdwarf-handles.hpp include/dwarfpp/libdwarf.hpp \
  include/dwarfpp/libdw.hpp include/dwarfpp/libdw-handles.hpp \
  include/dwarfpp/libdw-compat-constants.h \
  $(DWARF_LIB_HEADER)

lib_LTLIBRARIES = src/libdwarfpp.la

common_backing_SOURCES = src/libdwarf.cpp src/libdwarf-data.cpp
common_SOURCES = src/expr.cpp src/attr.cpp src/frame.cpp src/regs.cpp src/spec.cpp src/util.cpp src/root.cpp src/abstract.cpp src/iter.cpp src/dies.cpp
# The handle-method implementation is backend-specific: the libdw backend uses
# src/libdw-handles.cpp (alongside src/libdw.cpp); the libdwarf backend uses
# src/libdwarf-handles.cpp. The two are mutually exclusive.
if USE_LIBDW
src_libdwarfpp_la_SOURCES = $(common_backing_SOURCES) src/libdw.cpp src/libdw-handles.cpp $(common_SOURCES)
else
src_libdwarfpp_la_SOURCES = $(common_backing_SOURCES) src/libdwarf-handles.cpp $(common_SOURCES)
endif
src_libdwarfpp_la_LIBADD = $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS) -lsupc++ -lboost_filesystem
# leading space is a HACK to make this pass through 'automake'
 ifeq ($(libdwarf_libs),)
$(error No value for 'libdwarf_libs' so cannot build a correct libdwarfpp)
 endif
if USE_LIBDW
# libdw is a normal shared library; link it the ordinary way.
src_libdwarfpp_la_LDFLAGS = $(libdwarf_libs)
else
# libdwarf is vendored from a static archive, so pull in the whole archive.
src_libdwarfpp_la_LDFLAGS = -Wl,--whole-archive $(libdwarf_libs) -Wl,--no-whole-archive
endif

INC_PP = include/dwarfpp
# dwarf-lib.h symlinks David Anderson's libdwarf.h into our tree; it is only
# needed by the libdwarf backend (libdw.hpp pulls in elfutils' headers directly).
# Keep it out of both BUILT_SOURCES and the installed headers under libdw, since
# automake's all-am depends on $(HEADERS) and there is no libdwarf.h to symlink.
if USE_LIBDW
DWARF_LIB_BUILT_SOURCE =
DWARF_LIB_HEADER =
else
DWARF_LIB_BUILT_SOURCE = $(INC_PP)/dwarf-lib.h
DWARF_LIB_HEADER = $(INC_PP)/dwarf-lib.h
endif
BUILT_SOURCES = $(INC_PP)/dwarf-onlystd.h $(INC_PP)/dwarf-onlystd-v2.h $(INC_PP)/dwarf-ext-GNU.h $(INC_PP)/dwarf-current-adt.h $(INC_PP)/dwarf-current-factory.h $(DWARF_LIB_BUILT_SOURCE)
CLEANFILES = $(BUILT_SOURCES)

# A backend switch can leave stale automake dependency files behind
# Remove the dependency-tracking files on clean so they are regenerated from scratch
clean-local:
	-rm -f src/$(DEPDIR)/*.Po src/$(DEPDIR)/*.Plo
	-rm -f examples/$(DEPDIR)/*.Po examples/$(DEPDIR)/*.Plo

examplesdir = examples
examples_PROGRAMS = examples/compare-types examples/core-query examples/dwarfppdump \
  examples/print_spec examples/sranges examples/subseq examples/eh-frame-hdr

-include config.mk

EXAMPLE_LDFLAGS_INS = -L$(top_srcdir)/lib -Wl,-rpath,$(realpath $(top_srcdir))/lib
EXAMPLE_LDADD_INS = -ldwarfpp -lboost_filesystem -lelf -lz $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
examples_core_query_SOURCES = examples/core-query.cpp
examples_core_query_LDFLAGS = $(EXAMPLE_LDFLAGS_INS)
examples_core_query_LDADD = $(EXAMPLE_LDADD_INS)
examples_compare_types_SOURCES = examples/compare-types.cpp
examples_compare_types_LDFLAGS = $(EXAMPLE_LDFLAGS_INS)
examples_compare_types_LDADD = $(EXAMPLE_LDADD_INS)
examples_dwarfppdump_SOURCES = examples/dwarfppdump.cpp
examples_dwarfppdump_LDFLAGS = $(EXAMPLE_LDFLAGS_INS)
examples_dwarfppdump_LDADD = $(EXAMPLE_LDADD_INS)
examples_print_spec_SOURCES = examples/print_spec.cpp
examples_print_spec_LDFLAGS = $(EXAMPLE_LDFLAGS_INS)
examples_print_spec_LDADD = $(EXAMPLE_LDADD_INS)
examples_sranges_SOURCES = examples/sranges.cpp
examples_sranges_LDFLAGS = $(EXAMPLE_LDFLAGS_INS)
examples_sranges_LDADD = $(EXAMPLE_LDADD_INS)
examples_subseq_SOURCES = examples/subseq.cpp
examples_subseq_LDFLAGS = $(EXAMPLE_LDFLAGS_INS)
examples_subseq_LDADD = $(EXAMPLE_LDADD_INS)
examples_eh_frame_hdr_SOURCES = examples/eh-frame-hdr.cpp
examples_eh_frame_hdr_LDFLAGS = $(EXAMPLE_LDFLAGS_INS)
examples_eh_frame_hdr_LDADD = $(EXAMPLE_LDADD_INS)

# the following recipes create output using ">" so we need errors not to leave empty files
.DELETE_ON_ERROR:

# launder the exit code of grep through "cat"

# When backing with elfutils' libdw, the source dwarf.h uses C enums rather #defines
# So for the libdw backend we (a) rename the standard-header include guard so
# the filtered copy does not suppress the real <dwarf.h>, (b) append the
# constants libdw lacks, and (c) wrap each GNU enumerator in its own anonymous
# enum (elfutils drops the trailing comma at enum boundaries, so the grepped
# fragments are not otherwise valid where they are #included).
include/dwarfpp/dwarf-onlystd.h: $(libdwarf_includes)/dwarf.h
	cat "$<" | egrep -v 'DW_[A-Z]+_(GNU|SUN|HP|APPLE|INTEL|ARM|upc|PGI|ALTIUM|MIPS|CPQ|VMS|GNAT)' | \
		egrep -v '/\* (SGI|GNU)( \*/|\. )' | egrep -v 'LANG_Mips|LANG_Upc' | egrep -v '_use_GNAT' | egrep -v 'ATCF entries start at|DW_LANG_UPC instead.' | cat > "$@"
	if test "x$(DWARFPP_USE_LIBDW)" = x1; then \
		sed -i 's/_DWARF_H/_DWARFPP_ONLYSTD_H/g' "$@"; \
		echo '#include "dwarfpp/libdw-compat-constants.h"' >> "$@"; \
	fi

include/dwarfpp/dwarf-onlystd-v2.h: include/dwarfpp/dwarf-onlystd.h
	cat "$<" | grep -v 'DWARF[^2]' | cat > "$@"

include/dwarfpp/dwarf-ext-GNU.h: $(libdwarf_includes)/dwarf.h
	if test "x$(DWARFPP_USE_LIBDW)" = x1; then \
		cat "$<" | egrep '(_|/\* |, )GNU' | egrep -vi conflict | egrep -vi '^[[:blank:]]*/\*' | \
			awk '/^[[:space:]]*DW_[A-Za-z0-9_]+[[:space:]]*=/ { line=$$0; gsub(/,/, "", line); print "enum { " line " };"; next } { print }' > "$@"; \
	else \
		cat "$<" | egrep '(_|/\* |, )GNU' | egrep -vi conflict | egrep -vi '^[[:blank:]]*/\*' | cat > "$@"; \
	fi

include/dwarfpp/dwarf-current-adt.h: spec/gen-adt-cpp.py spec/dwarf_current.py
	spec/gen-adt-cpp.py > "$@"

include/dwarfpp/dwarf-current-factory.h: spec/gen-factory-cpp.py spec/dwarf_current.py
	spec/gen-factory-cpp.py > "$@"

# to avoid propagating libdwarf CFLAGS into all clients, symlink the libdwarf.h we use
# FIXME: support libdw1 as an alternative
include/dwarfpp/dwarf-lib.h: $(libdwarf_includes)/libdwarf.h
	(cd $(dir $@) && ln -s "$(realpath $<)" "$(notdir $@)" )

lib/libdwarfpp.so: $(lib_LTLIBRARIES)
	mkdir -p lib && cd lib && ln -sf ../src/.libs/libdwarfpp.so .

lib/libdwarfpp.so.0: $(lib_LTLIBRARIES)
	mkdir -p lib && cd lib && ln -sf ../src/.libs/libdwarfpp.so.0 .

lib/libdwarfpp.a: $(lib_LTLIBRARIES)
	mkdir -p lib && cd lib && ln -sf ../src/.libs/libdwarfpp.a .

all: lib/libdwarfpp.so lib/libdwarfpp.so.0  lib/libdwarfpp.a

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
