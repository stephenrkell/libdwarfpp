THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))
root := $(realpath $(dir $(THIS_MAKEFILE))/..)
CXXFLAGS += -I$(root)/include
LDFLAGS += -L$(root)/lib -Wl,-R$(root)/lib
LDFLAGS += -ldwarfpp -ldwarf -lelf -lsrk31c++ -lc++fileno -lboost_system

export CXXFLAGS
export LDFLAGS
export LDLIBS

cases := $(wildcard [a-z]*)

run-%: # run the test case with itself as input
	$(MAKE) -C "$*" "$*" && ( cd "$*" && ./$* ./$* )

gdbrun-%: # run the test case with itself as input
	$(MAKE) -C "$*" "$*" && ( cd "$*" && gdb --args ./$* ./$* )

cleanrun-%:
	rm -f $*/$* && $(MAKE) run-$*

gdbcleanrun-%:
	rm -f $*/$* && $(MAKE) gdbrun-$*

default:
	for case in $(cases); do \
            $(MAKE) run-$$case; \
        done
