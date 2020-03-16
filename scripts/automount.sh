#!/bin/sh

MOUNT="/bin/mount"
PMOUNT="/usr/bin/pmount"
UMOUNT="/bin/umount"
MOUNTOPTS="relatime"

# Parse parameters
if [ $# -lt 2 ]; then
	echo "Usage: $0 {add|remove} NAME"
	echo ""
	echo "Automatically mounts or unmounts a block device at /media/NAME if it"
	echo "contains a filesystem that we support."
	exit 1
fi
ACTION=$1
NAME=$(basename $2)
BLOCKDEV="/dev/${NAME}"

automount() {
	mkdir -p "/media/$NAME"
	if ! mount -t auto $BLOCKDEV "/media/$NAME"; then
		rmdir "/media/$NAME"
	else
		touch "/tmp/.automount-$NAME"
	fi
}

if [ "$ACTION" = "add" ] && [ -n "$BLOCKDEV" ]; then
	# Get the udev variables
	eval $(/sbin/blkid -o udev -p ${BLOCKDEV})

	# Blacklist filesystems in fstab and the root filesystem.
	if ! (cat /etc/fstab | awk '{print $1}' && findmnt -n -o SOURCE /) | grep -q "^${BLOCKDEV}$"; then
		 automount
	fi
fi


## Remove automounted directories.
if [ "$ACTION" = "remove" ] && [ -x "$UMOUNT" ] && [ -e "/tmp/.automount-$NAME" ]; then
	for mnt in $(lsblk -n -o MOUNTPOINT ${BLOCKDEV}); do /bin/umount -l $mnt; done
	rmdir "/media/$NAME"
	rm "/tmp/.automount-$NAME"
fi
