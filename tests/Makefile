THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))
root := $(realpath $(dir $(THIS_MAKEFILE))/..)
include $(root)/config.mk
$(warning PATH is ${PATH})
$(warning LD_LIBRARY_PATH is ${LD_LIBRARY_PATH})
CXXFLAGS += -I$(root)/include
CXXFLAGS += -g
CXXFLAGS += -std=c++14
LDFLAGS += -L$(root)/lib -Wl,-rpath,$(root)/lib
LDLIBS += -ldwarfpp -ldwarf -lelf -lsrk31c++ -lc++fileno -lboost_system -lz

export CXXFLAGS
export LDFLAGS
export LDLIBS
$(warning PATH is ${PATH})
$(warning LD_LIBRARY_PATH is ${LD_LIBRARY_PATH})

cases := $(filter-out makefile,$(wildcard [a-z]*))

default:
	for case in $(cases); do \
            $(MAKE) run-$$case || break; \
        done

clean:
	for case in $(cases); do \
            $(MAKE) clean-$$case; \
        done

clean-%: 
	rm -f $*/$*

build-%:
	$(MAKE) -C "$*" -f ../Makefile "$*"

run-%: # run the test case with itself as input
	$(MAKE) -C "$*" -f ../Makefile "$*" && ( cd "$*" && ./$* ./$* )
	
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

gdbrun-%: # run the test case with itself as input
	$(MAKE) -C "$*" -f ../Makefile "$*" && ( cd "$*" && gdb --args ./$* ./$* )

cleanrun-%:
	rm -f $*/$* && $(MAKE) run-$*

gdbcleanrun-%:
	rm -f $*/$* && $(MAKE) gdbrun-$*

grandchildren: LDFLAGS += -pthread -static
visible-named: LDFLAGS += -pthread -static
