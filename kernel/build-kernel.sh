#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
KERNEL_VERSION="6.1.22"
ARCH="${1:-amd64}"

case "$ARCH" in
    amd64)
        MAKE_ARCH=x86
        CROSS_COMPILE=""
        DEFCONFIG=""
        CONFIG_FILE="$SCRIPT_DIR/.config"
        KERNEL_PATH="arch/x86/boot/bzImage"
        OUTPUT="$SCRIPT_DIR/bzImage-$KERNEL_VERSION"
        EXTRA_CONFIG=()
        ;;
    arm64)
        MAKE_ARCH=arm64
        CROSS_COMPILE=aarch64-linux-gnu-
        DEFCONFIG=defconfig
        CONFIG_FILE="$SCRIPT_DIR/.config-arm64"
        KERNEL_PATH="arch/arm64/boot/Image"
        OUTPUT="$SCRIPT_DIR/Image-$KERNEL_VERSION-arm64"
        EXTRA_CONFIG=(
            VIRTIO VIRTIO_MMIO VIRTIO_BLK EXT4_FS
            DEVTMPFS DEVTMPFS_MOUNT
            SERIAL_AMBA_PL011 SERIAL_AMBA_PL011_CONSOLE
            NET_9P NET_9P_VIRTIO 9P_FS
        )
        ;;
    arm32)
        MAKE_ARCH=arm
        CROSS_COMPILE=arm-linux-gnueabihf-
        DEFCONFIG=multi_v7_defconfig
        CONFIG_FILE="$SCRIPT_DIR/.config-arm32"
        KERNEL_PATH="arch/arm/boot/zImage"
        OUTPUT="$SCRIPT_DIR/zImage-$KERNEL_VERSION-arm32"
        EXTRA_CONFIG=(
            VIRTIO VIRTIO_MMIO VIRTIO_BLK EXT4_FS
            DEVTMPFS DEVTMPFS_MOUNT
            SERIAL_AMBA_PL011 SERIAL_AMBA_PL011_CONSOLE
            NET_9P NET_9P_VIRTIO 9P_FS
        )
        ;;
    riscv64)
        MAKE_ARCH=riscv
        CROSS_COMPILE=riscv64-linux-gnu-
        DEFCONFIG=defconfig
        CONFIG_FILE="$SCRIPT_DIR/.config-riscv64"
        KERNEL_PATH="arch/riscv/boot/Image"
        OUTPUT="$SCRIPT_DIR/Image-$KERNEL_VERSION-riscv64"
        EXTRA_CONFIG=(
            VIRTIO VIRTIO_MMIO VIRTIO_BLK EXT4_FS
            DEVTMPFS DEVTMPFS_MOUNT
            SERIAL_8250 SERIAL_8250_CONSOLE SERIAL_8250_DW
            NET_9P NET_9P_VIRTIO 9P_FS
        )
        ;;
    *)
        echo "Usage: $0 [amd64|arm64|arm32|riscv64]"
        exit 1
        ;;
esac

cd "$SCRIPT_DIR"

if [ ! -d "linux-$KERNEL_VERSION" ]; then
    wget "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KERNEL_VERSION.tar.xz"
    tar -xf "linux-$KERNEL_VERSION.tar.xz"
fi

cd "linux-$KERNEL_VERSION"

if [ "$ARCH" = "amd64" ]; then
    cp "$CONFIG_FILE" .config
else
    make ARCH="$MAKE_ARCH" CROSS_COMPILE="$CROSS_COMPILE" "$DEFCONFIG"
    for option in "${EXTRA_CONFIG[@]}"; do
        scripts/config --enable "$option"
    done
    make ARCH="$MAKE_ARCH" CROSS_COMPILE="$CROSS_COMPILE" olddefconfig
    cp .config "$CONFIG_FILE"
fi

make -j"$(nproc)" ARCH="$MAKE_ARCH" CROSS_COMPILE="$CROSS_COMPILE"
cp "$KERNEL_PATH" "$OUTPUT"
echo "Kernel written to $OUTPUT"
