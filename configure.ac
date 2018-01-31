AC_INIT(hello, 0.1, okirby@krontech.ca)
AM_INIT_AUTOMAKE([subdir-objects -Wall -Werror foreign])
AM_SILENT_RULES([yes])
AM_PROG_AR
AC_PROG_CC
AC_PROG_RANLIB

## Set a conditional if a GCC sysroot is provided.
AC_ARG_VAR(SYSROOT,
	AS_HELP_STRING([Root path to use for finding headers and libraries]))
AM_CONDITIONAL(SYSROOT, [test "x$SYSROOT" != "x"])

## Build all tools only when compiling for ARM.
AS_CASE([$host],
	[arm*-linux-*], [XCOMPILE="camera"],
	[XCOMPILE="host"])
AM_CONDITIONAL(CAMBUILD, [test "$XCOMPILE" = "camera"])

PKG_CHECK_MODULES(GLIB, [glib-2.0])
PKG_CHECK_MODULES(DBUS, [dbus-glib-1])
PKG_CHECK_MODULES(GST, [gstreamer-0.10])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
	src/Makefile
])
AC_OUTPUT
