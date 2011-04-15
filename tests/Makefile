DWARF_PREFIX ?= $(HOME)/opt

ALL_TESTS := $(patsubst %-input,exec-%,$(wildcard test-*-input))
$(warning ALL_TESTS is $(ALL_TESTS))

CXX ?= g++

CFLAGS += $(INCLUDE_DIRS) -O0 -g3 -fno-eliminate-unused-debug-symbols -fno-eliminate-unused-debug-types -fkeep-inline-functions
CXXFLAGS += -std=c++0x $(INCLUDE_DIRS) -O0 -g3 -fno-eliminate-unused-debug-symbols -fno-eliminate-unused-debug-types -fkeep-inline-functions

CXXFLAGS += -Wl,-R$(realpath ../src)

default: $(ALL_TESTS)

test-input: test-2-input.c
	$(CC) -o "$@" $(CFLAGS) "$<"

test-%: test-%.cpp test-input ../src/libdwarfpp.so
	$(CXX) -o "$@" "$<" $(CXXFLAGS) $(LDFLAGS) -ldwarfpp -lsrk31c++ -ldwarf -lelf -lc++fileno -lboost_regex

test-iterator-comp: test-iterator-comp.cpp
	$(CXX) -o "$@" "$<" $(CXXFLAGS) $(LDFLAGS) -ldwarfpp -lsrk31c++ -ldwarf -lelf -lc++fileno -lboost_regex

exec-test-%: test-% test-input
	gdb --eval-command run --args ./test-$* test-input

nodbg-test-%: test-% test-%-input
	./test-$* test-input

test-8.dot: test-8 test-input
	(echo 'digraph blah {'; \
 ./test-8 test-8-input 2>&1  | grep '^Node' | \
 sed 's/Node \(.*\) depends on node \(.*\) owing to.*/"\1" -> "\2"/'; echo '}') > "$@"

.PHONY: clean
clean:
	rm -f $(ALL_TESTS)
