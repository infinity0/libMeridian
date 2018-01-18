#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(MeridianProcess.cpp, 1.1, bwong@cs.cornell.edu)
AM_INIT_AUTOMAKE(libMeridian, 1.1)
AC_CONFIG_SRCDIR([Common.h])
AC_CONFIG_HEADER([config.h])
AC_PROG_INSTALL

# Checks for programs.
AC_PROG_YACC
AC_PROG_CXX
AC_PROG_CC
AC_PROG_LEX
AC_PROG_RANLIB

# Temporary hack, the location of cblas in atlas
# is in the atlas directory
LIBS="-L/usr/lib/atlas -L/usr/lib64/atlas ${LIBS}"
# Checks for libraries.
AC_CHECK_LIB([cblas], [cblas_dcopy], , AC_MSG_ERROR(Library cblas required))
AC_CHECK_LIB([blas], [atl_f77wrap_cgemm_], , AC_MSG_ERROR(Library blas required))
AC_CHECK_LIB([crypto], [MD5_Init], , AC_MSG_ERROR(Library crypto required))
AC_CHECK_LIB([curl], [curl_easy_init], , AC_MSG_ERROR(Library curl required))
AC_CHECK_LIB([dl], [dlopen], , AC_MSG_ERROR(Library dl required))
AC_CHECK_LIB([gfortran], [_gfortran_f2c_specific__abs_r4], , AC_MSG_ERROR(Library gfortran required))
AC_CHECK_LIB([qhull], [qh_freeqhull], , AC_MSG_ERROR(Library qhull required))
AC_CHECK_LIB([z], [deflate], , AC_MSG_ERROR(Library z required))

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h limits.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h sys/param.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([floor gethostbyname gethostname gettimeofday inet_ntoa memmove memset pow select socket sqrt strchr])

#AC_CONFIG_FILES([Makefile])
AC_OUTPUT(Makefile)

