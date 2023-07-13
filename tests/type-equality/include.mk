# HACK: we have to repeat the rule for building from .cpp, to avoid use of '$+'
%: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)
# ... because ours also depends on test.os
type-equality: type-equality.cpp
type-equality: test.os

# test.o is a combined .o with exactly two CUs, which contain
# identical DWARF types. We make it from two copies
# of the same .o file. Which .o file? For now we want something
# reasonably substantial and written in C. Use the first .o file
# from libdwarf.a? Problem is that it might not have any DWARF,
# if the config gets it from the system. So alwyas get it from contrib.
#$(info LIBDWARF_A is $(LIBDWARF_A))
test.o:
	$(MAKE) -C ../../contrib build-libdwarf
	$(AR) x ../../contrib/libdwarf/libdwarf/libdwarf.a dwarf_original_elf_init.o
	mv dwarf_original_elf_init.o test.o

test.os: test.o test-renamed.o #hello.o hello-renamed.o
	ld -r -o $@ $+

%-renamed.o: %.o
	objcopy --prefix-symbols=another_ $< $@
