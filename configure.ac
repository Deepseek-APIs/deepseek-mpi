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

AC_CHECK_HEADERS([readline/readline.h], [], [AC_MSG_ERROR([readline/readline.h is required])])
AC_CHECK_HEADERS([readline/history.h], [], [AC_MSG_ERROR([readline/history.h is required])])
AC_CHECK_LIB([readline], [readline],
             [READLINE_LIBS="-lreadline"],
             [AC_MSG_ERROR([GNU Readline library is required])])
READLINE_CFLAGS=""
AC_SUBST([READLINE_LIBS])
AC_SUBST([READLINE_CFLAGS])

have_libmagic=no
PKG_CHECK_MODULES([LIBMAGIC], [libmagic], [have_libmagic=yes], [have_libmagic=no])
AS_IF([test "x$have_libmagic" = "xyes"], [AC_DEFINE([HAVE_LIBMAGIC], [1], [Define if libmagic is available])])

have_libxml2=no
PKG_CHECK_MODULES([LIBXML2], [libxml-2.0], [have_libxml2=yes], [have_libxml2=no])
AS_IF([test "x$have_libxml2" = "xyes"], [AC_DEFINE([HAVE_LIBXML2], [1], [Define if libxml2 is available])])

have_libarchive=no
PKG_CHECK_MODULES([LIBARCHIVE], [libarchive], [have_libarchive=yes], [have_libarchive=no])
AS_IF([test "x$have_libarchive" = "xyes"], [AC_DEFINE([HAVE_LIBARCHIVE], [1], [Define if libarchive is available])])

have_poppler=no
PKG_CHECK_MODULES([POPPLER], [poppler-glib], [have_poppler=yes], [have_poppler=no])
AS_IF([test "x$have_poppler" = "xyes"], [AC_DEFINE([HAVE_POPPLER_GLIB], [1], [Define if poppler-glib is available])])

have_tesseract=no
PKG_CHECK_MODULES([TESSERACT], [tesseract], [have_tesseract=yes], [have_tesseract=no])
AS_IF([test "x$have_tesseract" = "xyes"], [AC_DEFINE([HAVE_TESSERACT], [1], [Define if Tesseract OCR is available])])
AS_IF([test "x$have_tesseract" != "xyes"], [TESSERACT_CFLAGS=""; TESSERACT_LIBS=""])

AC_ARG_WITH([doxygen], AS_HELP_STRING([--with-doxygen=PATH], [Path to doxygen binary]),
  [DOXYGEN="$withval"],
  [AC_PATH_PROG([DOXYGEN], [doxygen], [no])])
AM_CONDITIONAL([HAVE_DOXYGEN], [test "x$DOXYGEN" != "xno"])

AC_SUBST([LIBCURL_CFLAGS])
AC_SUBST([LIBCURL_LIBS])
AC_SUBST([NCURSES_CFLAGS])
AC_SUBST([NCURSES_LIBS])
AC_SUBST([POPPLER_CFLAGS])
AC_SUBST([POPPLER_LIBS])
AC_SUBST([TESSERACT_CFLAGS])
AC_SUBST([TESSERACT_LIBS])
AC_SUBST([DOXYGEN])

AC_CONFIG_FILES([
  Makefile
  src/Makefile
  tests/Makefile
  doc/Makefile
  doc/Doxyfile
])
AC_OUTPUT
