CXX ?= g++

CXXFLAGS += -std=c++0x
ifeq ($(DEBUG),)
CXXFLAGS += -O4 -g3
else
CXXFLAGS += -O0 -g3 
endif

CXXFLAGS += -I../include
LDFLAGS += -L../lib -Wl,-rpath,$(realpath ../lib)

# while we sort out the old libdwarfpp stuff
CXXFLAGS += -Wno-deprecated-declarations
# really necessary?
CXXFLAGS += -fno-eliminate-unused-debug-symbols -fno-eliminate-unused-debug-types

HDRS := $(wildcard *.hpp)
SRC := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp,%.o,$(SRC))
PROGS := $(patsubst %.cpp,%,$(SRC))

.PHONY: default 
default: $(PROGS)

# cancel builtin rules for building executables
$(PROGS): %: %.o

$(PROGS): %: %.cpp

DEPS := $(patsubst %.cpp,.%.d,$(SRC))
$(DEPS): .%.d : %.cpp
	$(CXX) -MM $(CXXFLAGS) "$<"  > "$@"

-include $(DEPS)


.PHONY: test
test: dwarfppdump 
	./dwarfppdump ./dwarfppdump >/dev/null

# We link this in a weird way, so that ltrace -l/path/to/libdwarf.so works.
# And we use ltrace ( to optimise 
dwarfpplibdump: dwarfpplibdump.o ../src/libdwarfpp.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) \
	-Wl,-R$(realpath ../src) \
	-o "$@" "$<" ../src/libdwarfpp.a -ldwarf -lelf -lsrk31c++ -lboost_regex -lboost_system -lboost_serialization -lc++fileno



# our builtin depends on our library
%: %.o ../src/libdwarfpp.so
	$(CXX) $(CXXFLAGS) $(LDFLAGS) \
	-Wl,-R$(realpath ../src) \
	-o "$@" "$<" -ldwarfpp -ldwarf -lelf -lsrk31c++ -lboost_regex -lboost_serialization -lc++fileno

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c -o "$@" "$<"

.PHONY: clean
clean:
	rm -f $(PROGS) $(OBJS) $(DEPS)
