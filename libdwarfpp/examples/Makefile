HDRS := $(wildcard *.hpp)
SRC := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp,%.o,$(SRC))
PROGS := $(patsubst %.cpp,%,$(SRC))

.PHONY: default 
default: $(PROGS)

# cancel builtin rules for building executables
%: %.o

%: %.cpp

.PHONY: test
test: dwarfppdump 
	./dwarfppdump ./dwarfppdump >/dev/null

# our builtin depends on our library
%: %.o ../src/libdwarfpp.so
	g++ $(CXXFLAGS) $(LDFLAGS) \
	-Wl,-R$(realpath ../src) \
	-o "$@" "$<" -ldwarfpp -ldwarf -lelf -lsrk31c++ -lboost_regex -lc++fileno

%.o: %.cpp $(HDRS)
	g++ $(CXXFLAGS) -c -o "$@" "$<"

.PHONY: clean
clean:
	rm -f $(PROGS) $(OBJS)
