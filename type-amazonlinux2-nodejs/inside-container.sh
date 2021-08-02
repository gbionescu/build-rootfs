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

# Install nodejs
curl -sL https://rpm.nodesource.com/setup_14.x | bash
yum install -y nodejs

# Install netcat
yum install -y nc

# Set password
echo "root:root" | chpasswd

# Start the ssh server to generate keys
systemctl start sshd

# Then, copy the newly configured system to the rootfs image:
mkdir /my-rootfs
mount /rootfs.ext4 /my-rootfs

# Add nodejs script
cat > /my-rootfs/test.js <<- EOF
function fibonacci_recursive(n)
{
        if (n <= 1) { return 1; }
        return fibonacci_recursive(n-1) + fibonacci_recursive(n-2);
}

console.log(fibonacci_recursive(40));
EOF

for d in bin etc lib lib64 root run sbin usr var; do tar c "/$d" | tar x -C /my-rootfs; done
for dir in dev proc run sys var tmp; do mkdir -p /my-rootfs/${dir}; done
umount /my-rootfs

# All done, exit docker shell
exit
