#!/bin/sh
set -e
### Customize me!
yum install -y \
      openssh-server \
      openssh-clients \
      iproute2 \
      net-tools \
      tar \
      procps

# Set password
echo "root:root" | chpasswd

# Start the ssh server to generate keys
systemctl start sshd

# Then, copy the newly configured system to the rootfs image:
mkdir /my-rootfs
mount /rootfs.ext4 /my-rootfs

for d in bin etc lib lib64 root run sbin usr var; do tar c "/$d" | tar x -C /my-rootfs; done
for dir in dev proc run sys var tmp; do mkdir -p /my-rootfs/${dir}; done
umount /my-rootfs

# All done, exit docker shell
exit
