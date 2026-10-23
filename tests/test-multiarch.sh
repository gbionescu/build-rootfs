#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
cd "$SCRIPT_DIR/.."

if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root"
    exit 1
fi

for arch in arm64 arm32 riscv64; do
    echo "=== Building ubuntu24.04 for $arch ==="
    ./build-rootfs.sh ubuntu24.04 --arch "$arch"
    ./tests/qemu-test.sh "$(realpath "rootfs_$arch.ext4")" "$arch"
    rm -f "rootfs_ubuntu24.04_$arch.ext4"
    mv "rootfs_$arch.ext4" "rootfs_ubuntu24.04_$arch.ext4"
done

echo "All multiarch tests passed"
