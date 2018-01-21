#!/bin/sh

ssh -oKexAlgorithms=+diffie-hellman-group1-sha1 root@192.168.12.1 \
	'/usr/share/ti/ti-uia/loggerSMDump.out 0x9e400000 0x100000 vpss'

