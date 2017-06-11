#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT

AC_PROG_CC

AC_CONFIG_SRCDIR([src/io-copy_stream.c])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])

AC_CHECK_FUNCS([fcntl])
AC_CHECK_FUNCS([pread])
AC_CHECK_FUNCS([sendfile])
AC_CHECK_HEADERS([fcntl.h])
AC_CHECK_HEADERS([sys/fcntl.h])
AC_CHECK_HEADERS([sys/sendfile.h])
AC_CHECK_SIZEOF(off_t)
AC_CHECK_SIZEOF(size_t)
AC_TYPE_OFF_T
AC_OUTPUT
