THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))
root := $(realpath $(dir $(THIS_MAKEFILE))/..)
# config.mk puts the libc++fileno and libsrk31c++ flags in CXXFLAGS and LDFLAGS/LDLIBS
include $(root)/config.mk

cases := $(filter-out makefile oprofile_data,$(shell cd $(root)/tests && find -mindepth 1 -maxdepth 1 -type d -name '[a-z]*' -print | while read f; do basename $$f ; done ))
$(info cases is $(cases))

TEST_DWARF_VERSION ?= 4
CXXFLAGS += -I$(root)/include -O0 -fPIC -g -gdwarf-$(TEST_DWARF_VERSION) -std=c++14
LDFLAGS += -L$(root)/lib -Wl,-rpath,$(root)/lib
LDLIBS += -ldwarfpp -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS) -lboost_system -lboost_regex -lboost_filesystem -lz -lzstd

.PHONY: default
default: all

# cancel builtin rule to avoid attempts at .o->executable via CC
%: %.o
# we want to use depfiles to rebuild tests, so let's write out the rule
%: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< -o $@ -MMD -MF .$*.d $(LDLIBS)
$(info depfiles we're maybe-including: $(patsubst %,.%.d,$(cases)))
-include $(patsubst %,.%.d,$(cases))

# these should be statically linked because they test multi-CU features
grandchildren: LDFLAGS += -pthread -static
visible-named: LDFLAGS += -pthread -static

# HACK: binutils >= 2.44 changed readelf -wF to show .eh_frame_hdr when present,
# instead of the CIE/FDE table from .eh_frame. Link these tests without
# .eh_frame_hdr so that readelf -wF still shows the expected .eh_frame output.
fde-decode: LDFLAGS += -Wl,--no-eh-frame-hdr
fde-print: LDFLAGS += -Wl,--no-eh-frame-hdr

# declare the dep, to ensure we don't test a stale binary
grandchildren: $(root)/lib/libdwarfpp.a
visible-named: $(root)/lib/libdwarfpp.a

# HACK: manually link these with libdwarf for now, because unlike the .so,
# the .a does not include it. And we must append -lelf, -lz and -lzstd
# (newer libelf.a's elf_compress.o references ZSTD)... sigh.
grandchildren: LDLIBS += $(LIBDWARF_LIBS) -lelf -lz -lzstd
visible-named: LDLIBS += $(LIBDWARF_LIBS) -lelf -lz -lzstd

# test case build recipes call back to us using $(MAKE) -f ../Makefile
# ... here we allow each case to define an include.mk for extra rules
-include include.mk

# EXCLUDE_CASES is a space-separated list of cases to skip (e.g. from CI)
EXCLUDE_CASES ?=
run_cases := $(filter-out $(EXCLUDE_CASES),$(cases))

.PHONY: all
all: $(foreach c,$(run_cases),run-$(c))

# quick hack for a status report
.PHONY: report
report:
	$(MAKE) -k 2>&1 | grep '^make:.*Error'

.PHONY: clean
clean:
	for case in $(cases); do \
            $(MAKE) clean-$$case; \
        done

.PHONY: clean-%
clean-%: 
	rm -f $*/$*

.PHONY: build-%
build-%:
	$(MAKE) -C "$*" -f ../Makefile "$*"

.PHONY: run-%
run-%: # run the test case with itself as input
	$(MAKE) -C "$*" -f ../Makefile "$*" && ( cd "$*" && ./$* ./$* )

.PHONY: profile-%
profile-%:
	$(MAKE) -C "$*" "$*" && \
        sudo opcontrol --deinit && \
        sudo sh -c "echo '0' > /proc/sys/kernel/nmi_watchdog" && \
        rm -rf oprofile_data && \
        sudo opcontrol --reset && \
        sudo opcontrol --start --no-vmlinux --separate=lib,kernel --callgraph=3 && \
        ( cd "$*" && ./$* ./$* ); \
        sudo opcontrol --deinit && \
        opreport -l --callgraph $(root)/src/libdwarfpp.so $*/$* && \
        opreport -l $(root)/src/libdwarfpp.so $*/$* | head -n15

.PHONY: gdbrun-%
gdbrun-%: # run the test case with itself as input
	$(MAKE) -C "$*" -f ../Makefile "$*" && ( cd "$*" && gdb --args ./$* ./$* )

.PHONY: cleanrun-%
cleanrun-%:
	rm -f $*/$* && $(MAKE) run-$*

.PHONY: gdbcleanrun-%
gdbcleanrun-%:
	rm -f $*/$* && $(MAKE) gdbrun-$*
