CXX ?= g++

CXXFLAGS += -std=gnu++0x -fkeep-inline-functions -Wall -fPIC 
ifeq ($(DEBUG),)
$(warning Optimised build)
CXXFLAGS += -O4 
else
$(warning Debug build)
CXXFLAGS += -O0 -g3 -fno-eliminate-unused-debug-types
endif
CXXFLAGS += -I../include
CXXFLAGS += -I../include/dwarfpp

# add dependencies on dynamic libs libdwarfpp.so should pull in
LDFLAGS += -lboost_serialization # why do we need this?

SRC := $(wildcard *.cpp)
DEPS := $(patsubst %.cpp,.%.d,$(SRC))
OBJS := $(patsubst %.cpp,%.o,$(SRC))
$(warning OBJS is $(OBJS))
#HDRS := $(wildcard *.hpp)

# HACK to support rebuilding with/without debug enabled.
# If libdwarfpp.so exists and does not have debug info,
# ensure we remake all object files with the new CXXFLAGS.

#HAVE_BUILT_WITH_DEBUG := $(shell test -r libdwarfpp.so && objdump -h libdwarfpp.so | grep debug && echo "yes" )
#ifeq ($(DEBUG),) # no debug...
#ifneq ($(HAVE_BUILT_WITH_DEBUG),) # .. but built with debug
#$(warning Previous build was debug)
#FORCE_REMAKE := 1
#else
#$(warning Previous build was optimised)
#endif
#else # yes debug...
#ifeq ($(HAVE_BUILT_WITH_DEBUG),) # .. but built without debug
#$(warning Previous build was optimised)
#FORCE_REMAKE := 1
#else
#$(warning Previous build was debug)
#endif
#endif

ifneq ($(FORCE_REMAKE),)
default: clean libdwarfpp.so libdwarfpp.a
else
default: libdwarfpp.so libdwarfpp.a
endif


$(DEPS): .%.d : %.cpp
	$(CXX) -MM $(CXXFLAGS) "$<"  > "$@"

-include $(DEPS)

%.o: %.cpp #$(HDRS)
	$(CXX) $(CXXFLAGS) -c -o "$@" "$<"

dwarf-onlystd.h: /usr/include/dwarf.h
	cat "$<" | egrep -v 'DW_[A-Z]+_(GNU|SUN|HP|APPLE|INTEL|ARM|upc|PGI|ALTIUM|MIPS|CPQ|VMS|GNAT)' | \
		egrep -v '/\* (SGI|GNU)( \*/|\. )' | egrep -v 'LANG_Mips|LANG_Upc' | egrep -v '_use_GNAT' | egrep -v 'ATCF entries start at|DW_LANG_UPC instead.' > "$@"

dwarf-onlystd-v2.h: dwarf-onlystd.h
	cat "$<" | grep -v 'DWARF[^2]'

dwarf-ext-GNU.h: /usr/include/dwarf.h
	cat "$<" | egrep '(_|/\* |, )GNU' | egrep -vi conflict | egrep -vi '^[[:blank:]]*/\*' > "$@"

libdwarfpp.a: $(OBJS)
	ar r "$@" $^

# use this version in distribution (avoid writing huge .o files)
#libdwarfpp.so: $(SRC)
#	$(CXX) $(CXXFLAGS) -shared -o "$@" $^

# use this version when developing (avoid recompiling whole of huge .so file)
libdwarfpp.so: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o "$@" $^ #-ldwarf -lelf

.PHONY: clean
clean:
	rm -f *.o *.a *.inc .*.d *.so

coretest: coretest.cpp spec.o
	$(CXX) $(CXXFLAGS) -O0 -g -DMAKE_TEST_PROGRAM -o "$@" "$<" spec.o $(LDFLAGS) -ldwarf -lelf -lc++fileno $(LDLIBS)
