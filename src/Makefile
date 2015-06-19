CXX ?= g++

CXXFLAGS += -std=gnu++14 -fkeep-inline-functions -Wall -fPIC 
ifneq ($(NDEBUG),)
$(warning Optimised build)
CXXFLAGS += -O3 -g3
else
$(warning Debug build)
CXXFLAGS += -O0 -g3 -fno-eliminate-unused-debug-types
endif
CXXFLAGS += -I../include
CXXFLAGS += -I../include/dwarfpp
CXXFLAGS += -Wno-deprecated-declarations # TEMPorary HACK

# add dependencies on dynamic libs libdwarfpp.so should pull in
LDLIBS += -lsrk31c++

SRC := $(wildcard *.cpp)
DEPS := $(patsubst %.cpp,.%.d,$(SRC))
OBJS := $(patsubst %.cpp,%.o,$(SRC))
$(warning OBJS is $(OBJS))
#HDRS := $(wildcard *.hpp)

# HACK to support rebuilding with/without debug enabled.
# If libdwarfpp.so exists and does not have debug info,
# ensure we remake all object files with the new CXXFLAGS.

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
#	$(CXX) $(CXXFLAGS) -shared -o "$@" $^ $(LDLIBS)

# NO, we can't do this, because ldwarf is an archive which is not PIC
# libdwarfpp.so: LDLIBS += -ldwarf
# use this version when developing (avoid recompiling whole of huge .so file)
libdwarfpp.so: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared -o "$@" $^ $(LDLIBS)

.PHONY: clean
clean:
	rm -f *.o *.a *.inc .*.d *.so

coretest: coretest.cpp spec.o
	$(CXX) $(CXXFLAGS) -O0 -g -DMAKE_TEST_PROGRAM -o "$@" "$<" spec.o $(LDFLAGS) -ldwarf -lelf -lc++fileno $(LDLIBS)
