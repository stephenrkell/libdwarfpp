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
