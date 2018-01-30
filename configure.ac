AC_INIT(hello, 0.1, okirby@krontech.ca)
AM_INIT_AUTOMAKE([subdir-objects -Wall -Werror foreign])
AM_SILENT_RULES([yes])
AM_PROG_AR
AC_PROG_CC
AC_PROG_RANLIB

PKG_CHECK_MODULES(GLIB, [glib-2.0])
PKG_CHECK_MODULES(DBUS, [dbus-glib-1])
PKG_CHECK_MODULES(GST, [gstreamer-0.10])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
	src/Makefile
])
AC_OUTPUT
