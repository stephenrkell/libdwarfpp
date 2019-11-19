dnl This is srk's version of AX_CHECK_FUNC_IN. The version in autoconf-archive
dnl seems to require that AC_LANG expands to the current language, which on my
dnl autoconf seems to no longer be true (it's empty). It was only needed to
dnl guard the use of #ifdef __cplusplus, which can be done unconditionally,
dnl so do that instead. This is based on ax_check_func_in.m4 which is
dnl Copyright © 2008 Guido U. Draheim guidod@gmx.de
dnl Copying and distribution of this file, with or without modification, are permitted in any medium without royalty provided the copyright notice and this notice are preserved. This file is offered as-is, without any warranty.
AC_DEFUN([AX_CHECK_FUNC_IN],
[AC_MSG_CHECKING([for $2 in $1])
AC_CACHE_VAL(ac_cv_func_$2,
[AC_TRY_LINK(
dnl Don't include <ctype.h> because on OSF/1 3.0 it includes <sys/types.h>
dnl which includes <sys/select.h> which contains a prototype for
dnl select.  Similarly for bzero.
[/* System header to define __stub macros and hopefully few prototypes,
    which can conflict with char $2(); below.  */
#include <assert.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Override any gcc2 internal prototype to avoid an error.  */
/* We use char because int might match the return type of a gcc2
    builtin and then its argument prototype would still apply.  */
char $2();
#ifdef __cplusplus
}
#endif
], [
/* The GNU C library defines this for functions which it implements
    to always fail with ENOSYS.  Some functions are actually named
    something starting with __ and the normal name is an alias.  */
#if defined (__stub_$2) || defined (__stub___$2)
choke me
#else
$2();
#endif
], eval "ac_cv_func_$2=yes", eval "ac_cv_func_$2=no")])
if eval "test \"`echo '$ac_cv_func_'$2`\" = yes"; then
  AC_MSG_RESULT(yes)
  ifelse([$3], , :, [$3])
else
  AC_MSG_RESULT(no)
ifelse([$4], , , [$4
])dnl
fi
])

