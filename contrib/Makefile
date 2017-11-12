THIS_MAKEFILE := $(realpath $(lastword $(MAKEFILE_LIST)))
CONTRIB_ROOT := $(realpath $(dir $(THIS_MAKEFILE)))

# things we need to build:
# - libc++fileno
# - libsrk31c++

default: env.sh config.mk \
build-libsrk31c++ build-libc++fileno build-libdwarf

.PHONY: clean default

# ----------------------------other libs that are autotools'd
# ----------------------------libc++fileno

build-libc++fileno: libc++fileno/config.status
	$(MAKE) -C libc++fileno

libc++fileno/config.status:
	cd libc++fileno && aclocal && \
                autoconf && \
                libtoolize && \
                automake --add-missing && \
                automake && \
                ./configure --prefix=$(CONTRIB_ROOT)/usr

clean::
	$(MAKE) -C libc++fileno clean

CONFIG_MK += \nLIBCXXFILENO_CXXFLAGS += -I$(CONTRIB_ROOT)/libc++fileno/include\n
CONFIG_MK += \nLIBCXXFILENO_LIBS += -L$(CONTRIB_ROOT)/libc++fileno/lib -Wl,-rpath,$(CONTRIB_ROOT)/libc++fileno/lib -lc++fileno\n

# ----------------------------libsrk31c++

.PHONY: build-libsrk31c++
build-libsrk31c++: libsrk31c++/config.status build-libc++fileno
	$(MAKE) -C libsrk31c++

libsrk31c++/config.status:
	cd libsrk31c++ && ./autogen.sh && \
	LIBCXXFILENO_CXXFLAGS="-I$(CONTRIB_ROOT)/libc++fileno/include" \
	LIBCXXFILENO_LIBS="-L$(CONTRIB_ROOT)/libc++fileno/lib -Wl,-rpath,$(CONTRIB_ROOT)/libc++fileno/lib" \
	 ./configure --prefix=$(CONTRIB_ROOT)/usr

clean::
	$(MAKE) -C libsrk31c++ clean

CONFIG_MK += \nLIBSRK31CXX_CXXFLAGS += -I$(CONTRIB_ROOT)/libsrk31c++/include\n
CONFIG_MK += \nLIBSRK31CXX_LIBS += -L$(CONTRIB_ROOT)/libsrk31c++/lib -Wl,-rpath,$(CONTRIB_ROOT)/libsrk31c++/lib -lsrk31c++\n

# ----------------------------libdwarf
.PHONY: build-libdwarf
build-libdwarf: libdwarf/config.status
	$(MAKE) -C libdwarf
	cd libdwarf/prefix && mkdir lib && cd lib && ln -s ../../libdwarf/libdwarf.a .
	cd libdwarf/prefix && mkdir bin && cd bin && ln -s ../../dwarfdump/dwarfdump .
	cd libdwarf/prefix && mkdir include && cd include && ln -s ../../libdwarf/libdwarf.h ../../libdwarf/dwarf.h .

libdwarf/config.status:
	cd libdwarf && mkdir -p prefix && CFLAGS=-g ./configure --prefix=`pwd`/prefix

# if we clean in libdwarf, we become unbuildable, so also rm config.status
clean::
	rm -rf libdwarf/prefix/*
	$(MAKE) -C libdwarf clean
	rm -f libdwarf/config.status

CONFIG_MK += \nLIBDWARF_CFLAGS += -I$(CONTRIB_ROOT)/libdwarf/prefix/include\n
CONFIG_MK += \nLIBDWARF_LIBS += -L$(CONTRIB_ROOT)/libdwarf/prefix/lib -ldwarf\n

# ----------------------------config.mk

varlist := CXXFLAGS CPPFLAGS CFLAGS LDFLAGS \
LIBCXXFILENO_CXXFLAGS LIBCXXFILENO_LIBS \
LIBSRK31CXX_CXXFLAGS LIBSRK31CXX_LIBS \
LIBDWARF_CFLAGS LIBDWARF_LIBS

CONFIG_MK += \n\nenv.sh:\n\tprintf '"'"'$(foreach var,$(varlist),export $(var)="$$($(var))"; )'"'"' >"$$@" || (rm -f "$$@"; false)

config.mk:
	printf '$(CONFIG_MK)' > "$@" || (rm -f "$@"; false)

env.sh: config.mk
	$(MAKE) -f "$<" "$@"

clean::
	rm -f config.mk env.sh
