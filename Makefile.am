ACLOCAL_AMFLAGS = -I m4
AM_CXXFLAGS = -std=c++11 -fkeep-inline-functions -Wall -Wno-deprecated-declarations -Iinclude -Iinclude/dwarfpp $(LIBSRK31CXX_CFLAGS) $(LIBCXXFILENO_CFLAGS)

extra_DIST = libdwarfpp.pc.in
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libdwarfpp.pc

dwarfpp_includedir = $(includedir)/dwarfpp
dwarfpp_include_HEADERS = include/dwarfpp/adt.hpp include/dwarfpp/dwarf-onlystd.h include/dwarfpp/frame.hpp include/dwarfpp/attr.hpp include/dwarfpp/dwarf-onlystd-v2.h include/dwarfpp/lib.hpp include/dwarfpp/core.hpp include/dwarfpp/encap_graph.hpp include/dwarfpp/opt.hpp include/dwarfpp/dwarf3-adt.h include/dwarfpp/encap.hpp include/dwarfpp/regs.hpp include/dwarfpp/dwarf3-factory.h include/dwarfpp/encap_sibling_graph.hpp include/dwarfpp/spec_adt.hpp include/dwarfpp/dwarf-ext-GNU.h include/dwarfpp/expr.hpp include/dwarfpp/spec.hpp

lib_LTLIBRARIES = src/libdwarfpp.la
src_libdwarfpp_la_SOURCES = src/adt.cpp src/encap_adt.cpp src/expr.cpp src/lib.cpp src/rep.cpp src/attr.cpp src/encap.cpp src/frame.cpp src/regs.cpp src/spec.cpp
src_libdwarfpp_la_LIBADD = -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)

INC_PP = include/dwarfpp
BUILT_SOURCES = $(INC_PP)/dwarf-onlystd.h $(INC_PP)/dwarf-onlystd-v2.h $(INC_PP)/dwarf-ext-GNU.h $(INC_PP)/dwarf3-adt.h $(INC_PP)/dwarf3-factory.h 
CLEANFILES = $(BUILT_SOURCES)

include/dwarfpp/dwarf-onlystd.h: $(includedir)/dwarf.h
	cat "$<" | egrep -v 'DW_[A-Z]+_(GNU|SUN|HP|APPLE|INTEL|ARM|upc|PGI|ALTIUM|MIPS|CPQ|VMS|GNAT)' | \
		egrep -v '/\* (SGI|GNU)( \*/|\. )' | egrep -v 'LANG_Mips|LANG_Upc' | egrep -v '_use_GNAT' | egrep -v 'ATCF entries start at|DW_LANG_UPC instead.' > "$@"

include/dwarfpp/dwarf-onlystd-v2.h: include/dwarfpp/dwarf-onlystd.h
	cat "$<" | grep -v 'DWARF[^2]' > "$@"

include/dwarfpp/dwarf-ext-GNU.h: $(includedir)/dwarf.h
	cat "$<" | egrep '(_|/\* |, )GNU' | egrep -vi conflict | egrep -vi '^[[:blank:]]*/\*' > "$@"

include/dwarfpp/dwarf3-adt.h: spec/gen-adt-cpp.py spec/dwarf3.py
	python spec/gen-adt-cpp.py > "$@"

include/dwarfpp/dwarf3-factory.h: spec/gen-factory-cpp.py spec/dwarf3.py
	python spec/gen-factory-cpp.py > "$@"

noinst_PROGRAMS = examples/core-query examples/dwarfppdump examples/dwarfppencapdump examples/dwarfpplibdump examples/print_spec examples/sranges examples/subseq

examples_core_query_SOURCES = examples/core-query.cpp
examples_dwarfppdump_SOURCES = examples/dwarfppdump.cpp
examples_dwarfppencapdump_SOURCES = examples/dwarfppencapdump.cpp
examples_dwarfpplibdump_SOURCES = examples/dwarfpplibdump.cpp
examples_print_spec_SOURCES = examples/print_spec.cpp
examples_sranges_SOURCES = examples/sranges.cpp
examples_subseq_SOURCES = examples/subseq.cpp

examples_core_query_LDADD = -Lsrc -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
examples_dwarfppdump_LDADD = -Lsrc -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
examples_dwarfppencapdump_LDADD = -Lsrc -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
examples_dwarfpplibdump_LDADD = -Lsrc -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
examples_print_spec_LDADD = -Lsrc -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
examples_sranges_LDADD = -Lsrc -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
examples_subseq_LDADD = -Lsrc -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)

check_PROGRAMS = tests/bfs-adt/bfs-adt tests/children-adt/children-adt tests/core-bfs/core-bfs tests/coretest/coretest tests/core-variadic/core-variadic tests/dfs-seq/dfs-seq tests/fde-decode/fde-decode tests/fde-print/fde-print tests/grandchildren/grandchildren tests/make-dies/make-dies tests/resolve/resolve tests/siblings-adt/siblings-adt tests/siblings-core/siblings-core tests/timed-dump-core/timed-dump-core tests/type-equality/type-equality tests/visible-named-core/visible-named-core

TESTS = $(check_PROGRAMS)

tests_bfs_adt_bfs_adt_SOURCES = tests/bfs-adt/bfs-adt.cpp
tests_children_adt_children_adt_SOURCES = tests/children-adt/children-adt.cpp
tests_core_bfs_core_bfs_SOURCES = tests/core-bfs/core-bfs.cpp
tests_coretest_coretest_SOURCES = tests/coretest/coretest.cpp
tests_core_variadic_core_variadic_SOURCES = tests/core-variadic/core-variadic.cpp
tests_dfs_seq_dfs_seq_SOURCES = tests/dfs-seq/dfs-seq.cpp
tests_fde_decode_fde_decode_SOURCES = tests/fde-decode/fde-decode.cpp
tests_fde_print_fde_print_SOURCES = tests/fde-print/fde-print.cpp
tests_grandchildren_grandchildren_SOURCES = tests/grandchildren/grandchildren.cpp
tests_make_dies_make_dies_SOURCES = tests/make-dies/make-dies.cpp
tests_resolve_resolve_SOURCES = tests/resolve/resolve.cpp
tests_siblings_adt_siblings_adt_SOURCES = tests/siblings-adt/siblings-adt.cpp
tests_siblings_core_siblings_core_SOURCES = tests/siblings-core/siblings-core.cpp
tests_timed_dump_core_timed_dump_core_SOURCES = tests/timed-dump-core/timed-dump-core.cpp
tests_type_equality_type_equality_SOURCES = tests/type-equality/type-equality.cpp
tests_visible_named_core_visible_named_core_SOURCES = tests/visible-named-core/visible-named-core.cpp

tests_bfs_adt_bfs_adt_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_children_adt_children_adt_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_core_bfs_core_bfs_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_coretest_coretest_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_core_variadic_core_variadic_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_dfs_seq_dfs_seq_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_fde_decode_fde_decode_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_fde_print_fde_print_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_grandchildren_grandchildren_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_make_dies_make_dies_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_resolve_resolve_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_siblings_adt_siblings_adt_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_siblings_core_siblings_core_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_timed_dump_core_timed_dump_core_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_type_equality_type_equality_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
tests_visible_named_core_visible_named_core_LDADD = -ldwarfpp -ldwarf -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS)
