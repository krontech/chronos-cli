AC_INIT([chronos-cli], v0.3.1-beta)
AM_INIT_AUTOMAKE([subdir-objects -Wall -Werror foreign])
AM_SILENT_RULES([yes])
AM_PROG_AR
AC_PROG_CC
AC_PROG_RANLIB
LT_INIT

## Set a conditional if a GCC sysroot is provided.
AC_SUBST(SYSROOT, ["$with_sysroot"])
AM_CONDITIONAL(SYSROOT, [test "x$with_sysroot" != "xno"])

## Build all tools only when compiling for ARM.
AC_CANONICAL_HOST
AS_CASE([$host],
	[arm*-linux-*], [XCOMPILE="camera"],
	[XCOMPILE="host"])
AM_CONDITIONAL(CAMBUILD, [test "$XCOMPILE" = "camera"])

GST_REQUIRED=0.10.32

PKG_CHECK_MODULES(GLIB, [glib-2.0])
PKG_CHECK_MODULES(DBUS, [dbus-glib-1])
PKG_CHECK_MODULES(GST, [
  gstreamer-0.10 >= $GST_REQUIRED
  gstreamer-base-0.10 >= $GST_REQUIRED
  gstreamer-controller-0.10 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer0.10-dev and libgstreamer-plugins-base0.10-dev.
      on RPM-based systems gstreamer0.10-devel, libgstreamer0.10-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])

AC_SYS_LARGEFILE

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
	Makefile
	src/Makefile
])
AC_OUTPUT
