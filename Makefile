CXXFLAGS += -std=gnu++0x -g

# where is dwarf.h? assume /usr/include/dwarf.h
DWARF_PREFIX ?= /usr

.PHONY: default
default: gen src examples lib #tests

config.mk:
	$(dir $(lastword $(MAKEFILE_LIST)))/configure

include config.mk

include/dwarfpp/%-adt.h: gen-adt-cpp.py spec/%.py
	python ./gen-adt-cpp.py > "$@"
	
include/dwarfpp/%-factory.h: gen-factory-cpp.py spec/%.py
	python ./gen-factory-cpp.py > "$@"
	
incs := $(wildcard include/dwarfpp/*.inc)
m4_hdrs := $(wildcard include/dwarfpp/*.hpp.m4)
hdrs := $(patsubst %.m4,%,$(m4_hdrs))

%: %.m4 $(incs)
	rm -f "$@" && m4 -I. < "$<" > "$@" && chmod a-w "$@"

.PHONY: clean
clean:

include/dwarfpp/dwarf-onlystd.h: $(DWARF_PREFIX)/include/dwarf.h
	cat "$<" | egrep -v 'DW_[A-Z]+_(GNU|SUN|HP|APPLE|INTEL|ARM|upc|PGI|ALTIUM|MIPS|CPQ|VMS|GNAT)' | \
		egrep -v '/\* (SGI|GNU)( \*/|\. )' | egrep -v 'LANG_Mips|LANG_Upc' | egrep -v '_use_GNAT' | egrep -v 'ATCF entries start at|DW_LANG_UPC instead.' > "$@"

include/dwarfpp/dwarf-onlystd-v2.h: include/dwarfpp/dwarf-onlystd.h
	cat "$<" | grep -v 'DWARF[^2]' > "$@"

include/dwarfpp/dwarf-ext-GNU.h: $(DWARF_PREFIX)/include/dwarf.h
	cat "$<" | egrep '(_|/\* |, )GNU' | egrep -vi conflict | egrep -vi '^[[:blank:]]*/\*' > "$@"

.PHONY: gen
gen: include/dwarfpp/dwarf3-adt.h include/dwarfpp/dwarf3-factory.h \
    $(patsubst %.m4,%,$(m4_hdrs)) include/dwarfpp/dwarf-onlystd.h include/dwarfpp/dwarf-onlystd-v2.h include/dwarfpp/dwarf-ext-GNU.h

.PHONY: src
src: include gen
	$(MAKE) -C src

.PHONY: examples
examples: lib
	$(MAKE) -C examples

.PHONY: tests
tests: libs examples
	$(MAKE) -C tests

.PHONY: clean
clean:
	rm -f $(incs)
	rm -f $(hdrs)
	$(MAKE) -C src clean
	$(MAKE) -C examples clean
	$(MAKE) -C tests clean
	rm -f lib/*.so lib/*.a

.PHONY: lib
lib: src
	mkdir -p lib && cd lib && ln -sf ../src/libdwarfpp.* .

install:
	install -m 0644 -D -t $(INSTALL_PREFIX)/include/dwarfpp include/dwarfpp/*
	install -m 0644 -D -t $(INSTALL_PREFIX)/include/dwarfpp/private include/dwarfpp/private/*
	install -m 0644 -t $(INSTALL_PREFIX)/lib lib/*

