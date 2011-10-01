CXX ?= g++

CXXFLAGS += -std=gnu++0x -Wall -fPIC
ifeq ($(DEBUG),)
$(warning Optimised build)
CXXFLAGS += -O4 -g3
else
$(warning Debug build)
CXXFLAGS += -O0 -g3 -fno-eliminate-unused-debug-types -pg
endif
CXXFLAGS += -I../include

SRC := $(wildcard *.cpp)
DEPS := $(patsubst %.cpp,.%.d,$(SRC))
OBJS := $(patsubst %.cpp,%.o,$(SRC))
#HDRS := $(wildcard *.hpp)

# HACK to support rebuilding with/without debug enabled.
# If libdwarfpp.so exists and does not have debug info,
# ensure we remake all object files with the new CXXFLAGS.

HAVE_BUILT_WITH_DEBUG := $(shell test -r libdwarfpp.so && objdump -h libdwarfpp.so | grep debug)
ifeq ($(DEBUG),) # no debug...
ifneq ($(HAVE_BUILT_WITH_DEBUG),) # .. but built with debug
FORCE_REMAKE := 1
endif
else # yes debug...
ifeq ($(HAVE_BUILT_WITH_DEBUG),) # .. but built without debug
FORCE_REMAKE := 1
endif
endif

ifneq ($(FORCE_REMAKE),)
default: clean libdwarfpp.so 
else
default: libdwarfpp.so 
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
	$(CXX) $(CXXFLAGS) -shared -o "$@" $^

.PHONY: clean
clean:
	rm -f *.o *.a *.inc .*.d *.so

