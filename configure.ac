AC_PREREQ([2.69])
AC_INIT([deepseek-mpi], [0.1.0], [support@deepseek.example])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([foreign subdir-objects parallel-tests color-tests])
AC_USE_SYSTEM_EXTENSIONS

AC_PROG_CC
AC_PROG_INSTALL

AC_ARG_VAR([MPICC], [MPI C compiler wrapper (defaults to detected mpicc)])
if test "x$MPICC" != "x"; then
  CC="$MPICC"
else
  AC_PATH_PROG([MPICC], [mpicc], [no])
  if test "x$MPICC" != "xno"; then
    CC="$MPICC"
  fi
fi

AC_CHECK_HEADERS([mpi.h], [], [AC_MSG_ERROR([mpi.h is required to build deepseek-mpi])])
AC_SEARCH_LIBS([MPI_Init], [mpi mpi_ibm mpich], [], [AC_MSG_ERROR([Unable to find libmpi; ensure your MPI implementation is installed.])])

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([LIBCURL], [libcurl])
PKG_CHECK_MODULES([NCURSES], [ncurses])

AC_ARG_WITH([doxygen], AS_HELP_STRING([--with-doxygen=PATH], [Path to doxygen binary]),
  [DOXYGEN="$withval"],
  [AC_PATH_PROG([DOXYGEN], [doxygen], [no])])
AM_CONDITIONAL([HAVE_DOXYGEN], [test "x$DOXYGEN" != "xno"])

AC_SUBST([LIBCURL_CFLAGS])
AC_SUBST([LIBCURL_LIBS])
AC_SUBST([NCURSES_CFLAGS])
AC_SUBST([NCURSES_LIBS])
AC_SUBST([DOXYGEN])

AC_CONFIG_FILES([
  Makefile
  src/Makefile
  doc/Makefile
  doc/Doxyfile
])
AC_OUTPUT
