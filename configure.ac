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

AC_CHECK_HEADERS([elf.h libelf.h], [], [AC_MSG_FAILURE([required headers: elf.h libelf.h])])
AC_CHECK_HEADERS([dwarf.h libdwarf.h], [], [AC_MSG_FAILURE([required headers: dwarf.h libdwarf.h])])

AC_ARG_WITH([libdwarf-includes],
            [AS_HELP_STRING([--with-libdwarf-includes=DIR],
              [where to find libdwarf headers])],
            [
                if test -d "$withval"
                then
                        ac_libdwarf_includes="$withval"
                else
                        AC_MSG_ERROR(--with-libdwarf-includes expected directory name)
                fi
            ],
            [ac_libdwarf_includes=/usr/include])

AC_SUBST([libdwarf_includes], [$ac_libdwarf_includes])
AC_CHECK_HEADERS([$ac_libdwarf_includes/dwarf.h $ac_libdwarf_includes/libdwarf.h], [], [AC_MSG_FAILURE([required headers: dwarf.h libdwarf.h (looked in $ac_libdwarf_includes)])])
CFLAGS="-I$libdwarf_includes $CFLAGS"
CXXFLAGS="-I$libdwarf_includes $CXXFLAGS"

AX_BOOST_BASE

AC_CHECK_HEADER([boost/iostreams/filtering_stream.hpp], [], [AC_MSG_FAILURE([boost::iostreams is required])])
AC_HAVE_LIBRARY(boost_iostreams, [], [AC_MSG_FAILURE([boost::iostreams is required])])
AC_CHECK_HEADER([boost/regex/concepts.hpp], [], [AC_MSG_FAILURE([boost::regex is required])])
AC_HAVE_LIBRARY(boost_regex, [], [AC_MSG_FAILURE([boost::regex is required])])
AC_CHECK_HEADER([boost/serialization/traits.hpp], [], [AC_MSG_FAILURE([boost::serialization is required])])
AC_HAVE_LIBRARY(boost_serialization, [], [AC_MSG_FAILURE([boost::iostreams is required])])
AC_CHECK_HEADER([boost/system/api_config.hpp], [], [AC_MSG_FAILURE([boost::system is required])])
AC_HAVE_LIBRARY(boost_system, [], [AC_MSG_FAILURE([boost::system is required])])

AC_CHECK_LIB([elf], [elf_new], [])
AC_CHECK_LIB([dwarf], [dwarf_init], [])

# If the user (sanely) supplied _CXXFLAGS, and not _CFLAGS, 
# duplicate the latter to the former.  See rant about pkg-config in Makefile.am.
# We save the old _CFLAGS.
USER_LIBCXXFILENO_CFLAGS="$LIBCXXFILENO_CFLAGS"
LIBCXXFILENO_CFLAGS="$LIBCXXFILENO_CXXFLAGS"
USER_LIBSRK31CXX_CFLAGS="$LIBSRK31CXX_CFLAGS"
LIBSRK31CXX_CFLAGS="$LIBSRK31CXX_CXXFLAGS"

# Now, all the information from pkg-config or from user _CXXFLAGS) is in _CFLAGS.
# We might also have USER_ _CFLAGS, if the user did what pkg.m4 stupidly told them.
# We use this information (i.e. propagate to _CXXFLAGS) only if the user *didn't* give us
# _CXXFLAGS.
AM_CONDITIONAL(SUPPLIED_LIBCXXFILENO_CFLAGS, [test x"$USER_LIBCXXFILENO_CFLAGS" != x && test x"$LIBCXXFILENO_CXXFLAGS" = x])
AM_CONDITIONAL(SUPPLIED_LIBSRK31CXX_CFLAGS, [test x"$USER_LIBSRK31CXX_CFLAGS" != x && test x"$LIBSRK31CXX_CXXFLAGS" = x])

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([LIBCXXFILENO], [libcxxfileno])
PKG_CHECK_MODULES([LIBSRK31CXX], [libsrk31cxx])

AC_OUTPUT([Makefile libdwarfpp.pc])
