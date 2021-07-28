#!/bin/bash

# converts an ext4 given as parameter $1 to an initrd

rootfs=$1

if [ $UID != 0 ]; then
    echo "Needs root"
    exit 1
fi

mkdir tmpdir
mount $1 tmpdir

cd tmpdir
find . -print0 | cpio --null --create --verbose --format=newc | tee > ../initrd
cd ..
umount tmpdir
rmdir tmpdir
