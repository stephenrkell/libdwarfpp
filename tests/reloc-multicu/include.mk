# Build the two-CU relocatable fixture that the reloc-multicu test reads.
# `ld -r` concatenates the two CUs' .debug_info/.debug_abbrev and leaves the
# second CU's debug_abbrev_offset as a relocation against .debug_abbrev -- the
# exact case the ET_REL relocation fix handles.
#
# The fixture is an order-only prerequisite so the implicit `%: %.cpp` rule
# still sees the .cpp as $<.
reloc-multicu: | reloc-multicu-fixture.o

reloc-multicu-fixture.o: cu_a.o cu_b.o
	ld -r -o $@ cu_a.o cu_b.o

cu_a.o: cu_a.c
	cc -gdwarf-4 -O0 -c -o $@ $<

cu_b.o: cu_b.c
	cc -gdwarf-4 -O0 -c -o $@ $<

clean-reloc-multicu-fixture:
	rm -f cu_a.o cu_b.o reloc-multicu-fixture.o
