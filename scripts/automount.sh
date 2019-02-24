#!/bin/sh

MOUNT="/bin/mount"
PMOUNT="/usr/bin/pmount"
UMOUNT="/bin/umount"
MOUNTOPTS="relatime"
name="`basename "$DEVNAME"`"

case "$ID_FS_TYPE" in
	vfat|ntfs)
		MOUNTOPTS="relatime,utf8,gid=100,umask=002"
		;;
	*)
		MOUNTOPTS="relatime"
		;;
esac

automount() {
	mkdir -p "/media/$name"
	if ! mount -t auto $DEVNAME "/media/$name"; then
		rmdir "/media/$name"
	else
		touch "/tmp/.automount-$name"
	fi
}

if [ "$ACTION" = "add" ] && [ -n "$DEVNAME" ]; then
	if [ -x "$PMOUNT" ]; then
		echo $PMOUNT $DEVNAME > /tmp/automount-pmount-$name
		$PMOUNT $DEVNAME 2> /dev/null
	elif [ -x $MOUNT ]; then
		echo $PMOUNT $DEVNAME > /tmp/automount-pmount-$name
    		$MOUNT $DEVNAME 2> /dev/null
	fi

	if ! (cat /proc/mounts | awk '{print $1}' && readlink -f /dev/root) | grep -q "^$DEVNAME$"; then
		 automount
	fi
fi


## Remove automounted directories.
if [ "$ACTION" = "remove" ] && [ -x "$UMOUNT" ] && [ -n "$DEVNAME" ]; then
	for mnt in `cat /proc/mounts | grep "$DEVNAME" | cut -f 2 -d " " `
	do
		/bin/umount -l $mnt
	done
	
	# Remove empty directories from auto-mounter
	if [ -e "/tmp/.automount-$name" ]; then
		rmdir "/media/$name"
		rm "/tmp/.automount-$name"
	fi
fi
