#!/bin/sh
set -e
SERIAL_CONSOLE="${SERIAL_CONSOLE:-ttyS0}"

### Customize me!
DEBIAN_FRONTEND="noninteractive" apt-get install -y \
      openssh-server \
      openssh-client \
      iproute2 \
      net-tools strace

# Enable SSH
echo "PermitRootLogin yes" >> /etc/ssh/sshd_config
systemctl enable ssh

# Set password
echo "root:root" | chpasswd

# Enable the serial console; it starts on boot
systemctl enable "getty@${SERIAL_CONSOLE}"

# Then, copy the newly configured system to the rootfs image:
mkdir /my-rootfs
mount /rootfs.ext4 /my-rootfs

for d in bin etc lib lib64 root run sbin usr var; do
    if [ -e "/$d" ]; then
        tar c "/$d" | tar x -C /my-rootfs
    fi
done
for dir in dev proc run sys var tmp; do mkdir -p "/my-rootfs/${dir}"; done
umount /my-rootfs

# All done, exit docker shell
exit
