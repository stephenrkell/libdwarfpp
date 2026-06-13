# HACK: we have to repeat the rule for building from .cpp, to avoid use of '$+'
%: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)
# ... because ours also depends on test.os
type-equality: type-equality.cpp
type-equality: test.os

# test.os is a combined .o with two CUs that contain identical DWARF types
# (which is what this test deduplicates). We make it from two copies of the
# same C object, with the symbols of one renamed. We build that object from the
# local hello.c, pinned to DWARF 4, because the legacy libdwarf backend cannot
# parse the DWARF 5 a modern compiler emits by default. (It previously extracted
# an object from contrib libdwarf.a, which is DWARF 5 under a modern toolchain.)
test.o: hello.c
	$(CC) -gdwarf-$(TEST_DWARF_VERSION) -g -c $< -o $@

test.os: test.o test-renamed.o #hello.o hello-renamed.o
	ld -r -o $@ $+

%-renamed.o: %.o
	objcopy --prefix-symbols=another_ $< $@
