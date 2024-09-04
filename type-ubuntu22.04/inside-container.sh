#!/bin/sh
set -e
### Customize me!
DEBIAN_FRONTEND="noninteractive" apt-get install -y \
      openssh-server \
      openssh-client \
      iproute2 \
      net-tools strace

# Enable SSH
echo "PermitRootLogin yes" >> /etc/ssh/sshd_config

# Set password
echo "root:root" | chpasswd

# Set up serial
systemctl enable getty@ttyS0
systemctl start getty@ttyS0

# Then, copy the newly configured system to the rootfs image:
mkdir /my-rootfs
mount /rootfs.ext4 /my-rootfs

for d in bin etc lib lib64 root run sbin usr var; do tar c "/$d" | tar x -C /my-rootfs; done
for dir in dev proc run sys var tmp; do mkdir -p /my-rootfs/${dir}; done
umount /my-rootfs

# All done, exit docker shell
exit
