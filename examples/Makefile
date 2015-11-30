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

LDLIBS += -ldwarfpp -ldwarf -lelf

.PHONY: default 
default: $(PROGS)

DEPS := $(patsubst %.cpp,.%.d,$(SRC))
$(DEPS): .%.d : %.cpp
	$(CXX) -MM $(CXXFLAGS) "$<"  > "$@"

%: %.cpp
	$(CXX) $(CXXFLAGS) -o "$@" "$<" $(LDFLAGS) $(LDLIBS)

-include $(DEPS)

.PHONY: test
test: dwarfppdump 
	./dwarfppdump ./dwarfppdump >/dev/null

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
