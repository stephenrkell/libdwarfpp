DWARF_PREFIX ?= /usr

CXXFLAGS += -std=gnu++0x -g

.PHONY: default
default: include libs examples lib #tests
	
include/dwarfpp/%-adt.h: gen-adt-cpp.py spec/%.py
	python ./gen-adt-cpp.py > "$@"
	
include/dwarfpp/%-factory.h: gen-factory-cpp.py spec/%.py
	python ./gen-factory-cpp.py > "$@"

.PHONY: gen
gen: include/dwarfpp/dwarf3-adt.h include/dwarfpp/dwarf3-factory.h

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
	rm -f lib/*.so lib/*.a

.PHONY: lib
lib: libs
	mkdir -p lib && cd lib && ln -sf ../src/libdwarfpp.* .
