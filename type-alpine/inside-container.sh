#!/bin/sh
set -e
# Based on:
# https://github.com/firecracker-microvm/firecracker/blob/master/docs/rootfs-and-kernel-setup.md

### Customize me!
apk add openrc
apk add util-linux

# Set up ssh access
apk add bash dropbear haveged
mkdir -p /etc/dropbear
rc-update add haveged
###

### Post install stuff
# Set up a login terminal on the serial console (ttyS0):
ln -s agetty /etc/init.d/agetty.ttyS0
echo ttyS0 > /etc/securetty
rc-update add agetty.ttyS0 default

# Set default password
echo "root:root" | chpasswd

# Enable autologin
echo "agetty_options=\"--autologin root\"" > /etc/conf.d/agetty-autologin

### Make sure special file systems are mounted on boot:
rc-update add devfs boot
rc-update add procfs boot
rc-update add sysfs boot

# Enable local.d scripts
rc-update add local default

# Create startup script
cat <<EOF > /etc/local.d/RunMe.start
# Mount ramfs in /tmp
mkdir /tmp
mount -t tmpfs tmpfs /tmp

# Create an overlayfs over /etc
# dropbear uses /etc to create keys
mkdir -p /tmp/etc/work
mkdir -p /tmp/etc/upper

mount -t overlay \
      -o lowerdir=/etc,upperdir=/tmp/etc/upper,workdir=/tmp/etc/work \
       overlay /etc

dropbear -RBE&
EOF
chmod +x /etc/local.d/RunMe.start

# Then, copy the newly configured system to the rootfs image:
mkdir /my-rootfs
mount /rootfs.ext4 /my-rootfs

for d in bin etc lib root sbin usr; do tar c "/$d" | tar x -C /my-rootfs; done
for dir in dev proc run sys var tmp; do mkdir /my-rootfs/${dir}; done
umount /my-rootfs

# All done, exit docker shell
exit
