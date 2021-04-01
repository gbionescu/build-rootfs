#!/bin/bash
###
# Build a rootfs using an Alpine container
###

set -e

if [ $UID != 0 ]; then
    echo "Needs root"
    exit 1
fi

### Startup things
rm rootfs.ext4 || true
dd if=/dev/zero of=rootfs.ext4 bs=4M count=20
mkfs.ext4 rootfs.ext4

mkdir -p /tmp/my-rootfs
mount rootfs.ext4 /tmp/my-rootfs

sudo docker run -it --rm \
    -v /tmp/my-rootfs:/my-rootfs \
    -v $(pwd)/docker_rootfs_builder:/run_me \
    -v $(pwd)/alpine_boot:/alpine_boot \
    alpine "/run_me"

# Clean up
umount /tmp/my-rootfs
