CXXFLAGS := -std=c++14

.PHONY: default all
default: all

ELFUTILS_INCLUDE_DIR ?= /usr/include/elfutils
ELFUTILS_LIB_DIR ?= /usr/lib/`$(CC) -dumpmachine`

# it's important we have the matching elfutils' dwarf.h, for dump-enums to build correctly
ELFUTILS_DWARF_H ?= $(ELFUTILS_INCLUDE_DIR)/../dwarf.h

include config.mk

BASIC_CXXFLAGS := $(CPPFLAGS) $(CXXFLAGS) -I$(ELFUTILS_INCLUDE_DIR)
BASIC_CXXFLAGS += -fno-omit-frame-pointer -std=c++1y -ggdb3 -fvar-tracking-assignments \
  -O2 -fkeep-inline-functions -Wall -Wno-deprecated-declarations

CXXFLAGS := $(BASIC_CXXFLAGS) \
   -Iinclude -Iinclude/dwarfpp \
  $(LIBSRK31CXX_CFLAGS) $(LIBCXXFILENO_CFLAGS)

elfutils_libs ?= -ldw
src/libdwarfpp.so: LDFLAGS += -Wl,--whole-archive $(elfutils_libs) -Wl,--no-whole-archive
# FIXME: instead of static-linking the archive, consider making libdwarfpp.so a linker script
#  that adds libdw1.so to the link

INC_PP := include/dwarfpp
built_sources := $(INC_PP)/dwarf-all.h $(INC_PP)/dwarf-onlystd.h $(INC_PP)/dwarf-onlystd-v2.h $(INC_PP)/dwarf-ext-GNU.h $(INC_PP)/dwarf-current-adt.h $(INC_PP)/dwarf-current-factory.h $(INC_PP)/dwarf-lib.h
.PHONY: sources
sources: $(built_sources)

clean::
	rm -f $(built_sources)

dump-enums: LDLIBS += $(elfutils_libs)
dump-enums: LDFLAGS := -L$(ELFUTILS_LIB_DIR)
dump-enums: CPPFLAGS += -include $(ELFUTILS_DWARF_H) -I$(ELFUTILS_INCLUDE_DIR)

# FIXME: add -g -fno-eliminate-unused-debug-types
# (I've a feeling we need to define a whole new makerule,
# rather than being able to add to the flags specifically.)
include/dwarfpp/dwarf-all.h: dump-enums
	./$< > $@ || (rm -f $@; false)

# launder the exit code of grep through "cat"
include/dwarfpp/dwarf-onlystd.h: include/dwarfpp/dwarf-all.h
	cat "$<" | egrep -v 'DW_[A-Z]+_(GNU|SUN|HP|APPLE|INTEL|ARM|upc|PGI|ALTIUM|MIPS|CPQ|VMS|GNAT)' | \
		egrep -v '/\* (SGI|GNU)( \*/|\. )' | egrep -v 'LANG_Mips|LANG_Upc' | egrep -v '_use_GNAT' | egrep -v 'ATCF entries start at|DW_LANG_UPC instead.' | cat > "$@"

include/dwarfpp/dwarf-onlystd-v2.h: include/dwarfpp/dwarf-onlystd.h
	cat "$<" | grep -v 'DWARF[^2]' | cat > "$@"

include/dwarfpp/dwarf-ext-GNU.h: include/dwarfpp/dwarf-all.h
	cat "$<" | egrep '(_|/\* |, )GNU' | egrep -vi conflict | egrep -vi '^[[:blank:]]*/\*' | cat > "$@"

include/dwarfpp/dwarf-current-adt.h: spec/gen-adt-cpp.py spec/dwarf_current.py
	python2 spec/gen-adt-cpp.py > "$@"

include/dwarfpp/dwarf-current-factory.h: spec/gen-factory-cpp.py spec/dwarf_current.py
	python2 spec/gen-factory-cpp.py > "$@"

# to avoid propagating libdwarf CFLAGS into all clients, symlink the libdwarf.h we use
# FIXME: simultaneous support for both libdwarf and elfutils? Need configure-time magic....
include/dwarfpp/dwarf-lib.h: $(ELFUTILS_INCLUDE_DIR)/libdw.h
	(cd $(dir $@) && ln -s "$(realpath $<)" "$(notdir $@)" )

genhdrs := include/dwarfpp/dwarf-all.h \
   include/dwarfpp/dwarf-onlystd.h \
   include/dwarfpp/dwarf-onlystd-v2.h \
   include/dwarfpp/dwarf-ext-GNU.h \
   include/dwarfpp/dwarf-current-adt.h \
   include/dwarfpp/dwarf-current-factory.h \
   include/dwarfpp/dwarf-lib.h

.PHONY: genhdrs
genhdrs: $(genhdrs)

lib/libdwarfpp.so: src/libdwarfpp.so
	mkdir -p lib && cd lib && ln -sf ../src/.libs/libdwarfpp.so .

lib/libdwarfpp.a: src/libdwarfpp.so
	mkdir -p lib && cd lib && ln -sf ../src/.libs/libdwarfpp.a .

all: lib/libdwarfpp.so # lib/libdwarfpp.so.0  lib/libdwarfpp.a

objs := $(patsubst %.cpp,%.o,$(wildcard src/*.cpp))

CXXFLAGS += -MD
deps := $(patsubst %.cpp,%.d,$(wildcard src/*.cpp))
deps += dump-enums.d

# we want deps to trigger our .h building. I think that doesn't work, though.
# We have a circularity: need to run dump-enums before we can generate any additional
# headers or code.
-include $(deps)

src/libdwarfpp.so: $(patsubst %.cpp,%.o,$(wildcard src/*.cpp))
