#!/bin/sh
set -e
SERIAL_CONSOLE="${SERIAL_CONSOLE:-ttyS0}"

### Customize me!
DEBIAN_FRONTEND="noninteractive" apt-get install -y \
      git \
      gcc \
      libc6-dev \
      vim \
      sudo \
      ethtool \
      iputils-ping \
      net-tools \
      openssh-server \
      openssh-client \
      udhcpc \
      apt \
      ca-certificates \
      iproute2 \
      strace

# Enable SSH
echo "PermitRootLogin yes" >> /etc/ssh/sshd_config
systemctl enable ssh

# Set password
echo "root:root" | chpasswd

# QEMU user-mode networking: let udhcpc own resolv.conf
systemctl disable systemd-resolved 2>/dev/null || true
systemctl mask systemd-resolved.service 2>/dev/null || true
rm -f /sbin/resolvconf

cat > /etc/systemd/system/udhcpc.service <<'EOF'
[Unit]
Description=udhcpc DHCP client on eth0
After=network.target
ConditionPathExists=/sys/class/net/eth0

[Service]
Type=simple
ExecStart=/usr/sbin/udhcpc -f -i eth0 -R -s /etc/udhcpc/default.script
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

systemctl enable udhcpc.service

# Enable the serial console; it starts on boot
systemctl enable "getty@${SERIAL_CONSOLE}"

# Mount the host linux-app share on first access
mkdir -p /mnt/linux-app
if ! grep -q ' /mnt/linux-app ' /etc/fstab 2>/dev/null; then
    echo 'linuxapp /mnt/linux-app 9p trans=virtio,version=9p2000.L,nofail,x-systemd.automount 0 0' >> /etc/fstab
fi

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
