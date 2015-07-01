AC_CONFIG_MACRO_DIR([m4])
AC_PREREQ([2.69])
AC_INIT([libdwarfpp], [0.1], [srk31@cl.cam.ac.uk])
AC_CONFIG_SRCDIR([include/dwarfpp/lib.hpp])
CFLAGS="-I$prefix/include $CFLAGS"
CXXFLAGS="-I$prefix/include $CXXFLAGS"
LDFLAGS="-L$prefix/lib $LDFLAGS"
PKG_CONFIG_PATH="$prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
export CFLAGS CXXFLAGS LDFLAGS PKG_CONFIG_PATH
LT_INIT
AC_LANG([C++])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wno-portability])
AM_MAINTAINER_MODE
AC_PROG_CXX

AX_CXX_COMPILE_STDCXX_11
CPPFLAGS="-std=c++11 $CPPFLAGS"
export CXXFLAGS

AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_SIZE_T
AC_CHECK_TYPES([ptrdiff_t])
AC_PROG_LN_S
AC_PROG_INSTALL

AC_CHECK_HEADERS([algorithm cassert cstdlib dwarf.h elf.h functional iostream iterator libdwarf.h libelf.h limits map memory queue set stack string strings.h type_traits unistd.h unordered_map unordered_set utility vector], [], [AC_MSG_FAILURE([required standard headers: algorithm cassert cstdlib dwarf.h elf.h functional iostream iterator libdwarf.h libelf.h limits map memory queue set stack string strings.h type_traits unistd.h unordered_map unordered_set utility vector])])

AC_CHECK_HEADERS([dwarf.h elf.h libdwarf.h libelf.h], [], [AC_MSG_FAILURE([required headers: dwarf.h elf.h libdwarf.h libelf.h])])

AX_BOOST_BASE

AC_CHECK_HEADER([boost/iostreams/filtering_stream.hpp], [], [AC_MSG_FAILURE([boost::iostreams is required])])
AC_HAVE_LIBRARY(boost_iostreams, [], [AC_MSG_FAILURE([boost::iostreams is required])])
AC_CHECK_HEADER([boost/regex/concepts.hpp], [], [AC_MSG_FAILURE([boost::regex is required])])
AC_HAVE_LIBRARY(boost_regex, [], [AC_MSG_FAILURE([boost::regex is required])])
AC_CHECK_HEADER([boost/serialization/traits.hpp], [], [AC_MSG_FAILURE([boost::serialization is required])])
AC_HAVE_LIBRARY(boost_serialization, [], [AC_MSG_FAILURE([boost::iostreams is required])])
AC_CHECK_HEADER([boost/system/api_config.hpp], [], [AC_MSG_FAILURE([boost::system is required])])
AC_HAVE_LIBRARY(boost_system, [], [AC_MSG_FAILURE([boost::system is required])])


AC_HAVE_LIBRARY(elf, [], [AC_MSG_FAILURE([libelf is required])])
AC_HAVE_LIBRARY(dwarf, [], [AC_MSG_FAILURE([libdwarf is required])])

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([LIBCXXFILENO], [libcxxfileno])
PKG_CHECK_MODULES([LIBSRK31CXX], [libsrk31cxx])

AC_OUTPUT([Makefile libdwarfpp.pc])
