THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))
root := $(realpath $(dir $(THIS_MAKEFILE))/..)
# config.mk puts the libc++fileno and libsrk31c++ flags in CXXFLAGS and LDFLAGS/LDLIBS
include $(root)/config.mk

cases := $(filter-out makefile,$(wildcard [a-z]*))
$(info cases is $(cases))

CXXFLAGS += -I$(root)/include -O0 -fPIC -g -std=c++14
LDFLAGS += -L$(root)/lib -Wl,-rpath,$(root)/lib
LDLIBS += -ldwarfpp -lelf $(LIBSRK31CXX_LIBS) $(LIBCXXFILENO_LIBS) -lboost_system -lboost_regex -lboost_filesystem -lz

.PHONY: default
default: all

# cancel builtin rule to avoid attempts at .o->executable via CC
%: %.o

# these should be statically linked because they test multi-CU features
grandchildren: LDFLAGS += -pthread -static
visible-named: LDFLAGS += -pthread -static

# declare the dep, to ensure we don't test a stale binary
grandchildren: $(root)/lib/libdwarfpp.a
visible-named: $(root)/lib/libdwarfpp.a

# HACK: manually link these with libdwarf for now, because unlike the .so,
# the .a does not include it. And we must append -lelf and -lz... sigh.
grandchildren: LDLIBS += $(LIBDWARF_LIBS) -lelf -lz
visible-named: LDLIBS += $(LIBDWARF_LIBS) -lelf -lz

# test case build recipes call back to us using $(MAKE) -f ../Makefile
# ... here we allow each case to define an include.mk for extra rules
-include include.mk

.PHONY: all
all: $(foreach c,$(cases),run-$(c))

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
