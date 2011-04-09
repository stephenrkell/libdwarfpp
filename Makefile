DWARF_PREFIX ?= /usr

.PHONY: default
default: include libs examples lib #tests

incs := include/dwarfpp/abstract_hdr_gen.inc \
       include/dwarfpp/encap_hdr_gen.inc \
       include/dwarfpp/abstract_preamble_gen.inc \
       include/dwarfpp/encap_preamble_gen.inc \
       include/dwarfpp/adt_interfaces.inc \
       include/dwarfpp/encap_typedefs_gen.inc \
       src/encap_src_gen.inc \
       src/encap_factory_gen.inc \
       include/dwarfpp/encap_adt.hpp \
       include/dwarfpp/abstract.hpp

$(incs): adt-gen.py
	./adt-gen.py
	
include/dwarfpp/%-adt.h: gen-adt-cpp.py spec/%.py
	python ./gen-adt-cpp.py > "$@"
	
include/dwarfpp/%-factory.h: gen-factory-cpp.py spec/%.py
	python ./gen-factory-cpp.py > "$@"

.PHONY: gen
gen: $(incs) include/dwarfpp/dwarf3-adt.h include/dwarfpp/dwarf3-factory.h

.PHONY: include
include: gen
	$(MAKE) -C include

.PHONY: libs
libs: include gen
	$(MAKE) -C src

.PHONY: examples
examples: libs
	$(MAKE) -C examples

.PHONY: tests
tests: libs examples
	$(MAKE) -C tests

.PHONY: clean
clean:
	rm -f $(incs)
	$(MAKE) -C include clean
	$(MAKE) -C src clean
	$(MAKE) -C examples clean
	$(MAKE) -C tests clean
	rm -f lib/*.so

.PHONY: lib
lib: libs
	mkdir -p lib && cd lib && ln -sf ../src/libdwarfpp.* .
