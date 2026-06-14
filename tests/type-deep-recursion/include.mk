# HACK: repeat the rule for building from .cpp, to avoid use of '$+'
%: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

# This test runs its work on a thread with a small stack, so it needs pthread.
type-deep-recursion: LDFLAGS += -pthread
type-deep-recursion: type-deep-recursion.cpp
type-deep-recursion: deep2.os

# How many levels of nesting the synthetic type has. ~1000 reliably overflows a
# 256 KiB stack with the (old) unbounded recursion, while staying quick to build
# and to run with the fix.
DEEP_N ?= 1000

# Generate a deeply nested struct: each s_i contains s_{i-1} *by value* (the
# acyclic deepening that drove the stack overflow) plus a self-pointer (a
# recursive edge, to also exercise the assuming_equal cycle-breaking).
deep.c:
	@echo 'struct s0 { int x; };' > $@
	@i=1; while [ $$i -le $(DEEP_N) ]; do \
		echo "struct s$$i { struct s$$(($$i-1)) inner; struct s$$i *self; };" >> $@; \
		i=$$(($$i+1)); \
	done
	@echo 'struct s$(DEEP_N) g;' >> $@
	@echo 'int f(void) { return (int) sizeof(g); }' >> $@

# Build two identical CUs and combine them, so the deep type has a structurally
# equal duplicate to compare against (forcing deep equal()). We rename the
# symbols of the second copy so the linker does not see duplicate definitions;
# the DWARF (hence the types) stays identical. Pin the DWARF version because the
# legacy libdwarf backend cannot parse the DWARF 5 a modern compiler defaults to.
deep_a.o: deep.c
	$(CC) -gdwarf-$(TEST_DWARF_VERSION) -g -O0 -c $< -o $@

deep_b.o: deep_a.o
	objcopy --prefix-symbols=another_ $< $@

deep2.os: deep_a.o deep_b.o
	ld -r -o $@ $+
