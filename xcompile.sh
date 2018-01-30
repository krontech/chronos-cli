#!/bin/bash
CFGDIR=$(dirname ${BASH_SOURCE[0]})
SYSROOT="$1"
${CFGDIR}/configure --host=arm-linux-gnueabi \
	--prefix=${SYSROOT} SYSROOT=${SYSROOT} \
	PKG_CONFIG_LIBDIR=${SYSROOT}/usr/lib/pkgconfig \
	PKG_CONFIG_SYSROOT_DIR=${SYSROOT}
