#!/bin/bash
CFGDIR=$(dirname ${BASH_SOURCE[0]})
if [ "$#" -lt 1 ]; then
	echo "Missing argument: SYSROOT"
	exit 1
fi
if [ "$1" = "--help" ]; then
	echo "Usage: ${BASH_SOURCE[0]} SYSROOT [options]"
	echo ""
	echo "Configure cross compilation to target the Chronos camera. The"
	echo "SYSROOT argument must point to the root filesystem of the camera."
	${CFGDIR}/configure --help | tail -n +4
	exit 0
fi
SYSROOT="$1"
shift 1

${CFGDIR}/configure --host=arm-linux-gnueabi \
	--prefix=${SYSROOT} SYSROOT=${SYSROOT} \
	PKG_CONFIG_LIBDIR=${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/lib/arm-linux-gnueabi/pkgconfig \
	PKG_CONFIG_SYSROOT_DIR=${SYSROOT} "$@"
