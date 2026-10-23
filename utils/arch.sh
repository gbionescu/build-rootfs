#!/bin/bash
# Shared architecture definitions for rootfs builds and QEMU runs.

ARCH_UTILS_DIR="$( cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 ; pwd -P )"
KERNEL_DIR="$( cd "$ARCH_UTILS_DIR/.." >/dev/null 2>&1 ; pwd -P )/kernel"

VALID_ARCHS="amd64 arm64 arm32 riscv64"

normalize_arch() {
    case "$1" in
        ""|amd64|x86_64) echo "amd64" ;;
        arm64|aarch64) echo "arm64" ;;
        arm32|armhf|armv7|armv7l) echo "arm32" ;;
        riscv64) echo "riscv64" ;;
        *) return 1 ;;
    esac
}

type_supports_arch() {
    case "$1" in
        ubuntu22.04|ubuntu24.04) return 0 ;;
        *) [ "$2" = "amd64" ] ;;
    esac
}

set_arch_vars() {
    ARCH="$(normalize_arch "$1")" || return 1
    case "$ARCH" in
        amd64)
            DOCKER_PLATFORM="linux/amd64"
            QEMU_BIN="qemu-system-x86_64"
            KERNEL_IMAGE="$KERNEL_DIR/bzImage-6.1.22"
            SERIAL_CONSOLE="ttyS0"
            ROOT_DEVICE="/dev/sda"
            BINFMT_NAME="qemu-x86_64"
            ;;
        arm64)
            DOCKER_PLATFORM="linux/arm64/v8"
            QEMU_BIN="qemu-system-aarch64"
            KERNEL_IMAGE="$KERNEL_DIR/Image-6.1.22-arm64"
            SERIAL_CONSOLE="ttyAMA0"
            ROOT_DEVICE="/dev/vda"
            BINFMT_NAME="qemu-aarch64"
            ;;
        arm32)
            DOCKER_PLATFORM="linux/arm/v7"
            QEMU_BIN="qemu-system-arm"
            KERNEL_IMAGE="$KERNEL_DIR/zImage-6.1.22-arm32"
            SERIAL_CONSOLE="ttyAMA0"
            ROOT_DEVICE="/dev/vda"
            BINFMT_NAME="qemu-arm"
            ;;
        riscv64)
            DOCKER_PLATFORM="linux/riscv64"
            QEMU_BIN="qemu-system-riscv64"
            KERNEL_IMAGE="$KERNEL_DIR/Image-6.1.22-riscv64"
            SERIAL_CONSOLE="ttyS0"
            ROOT_DEVICE="/dev/vda"
            BINFMT_NAME="qemu-riscv64"
            ;;
    esac
}

qemu_boot_command() {
    local image="$1"
    case "$ARCH" in
        amd64)
            QEMU_CMD=("$QEMU_BIN" -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw earlyprintk=serial"
                      -nographic -m 1G -hda "$image")
            ;;
        arm64)
            QEMU_CMD=("$QEMU_BIN" -M virt -cpu cortex-a72 -m 1G -nographic
                      -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait earlyprintk"
                      -drive "file=$image,format=raw,if=none,id=hd0"
                      -device virtio-blk-device,drive=hd0)
            ;;
        arm32)
            QEMU_CMD=("$QEMU_BIN" -M virt -cpu cortex-a15 -m 1G -nographic
                      -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait earlyprintk"
                      -drive "file=$image,format=raw,if=none,id=hd0"
                      -device virtio-blk-device,drive=hd0)
            ;;
        riscv64)
            QEMU_CMD=("$QEMU_BIN" -M virt -m 1G -nographic -bios default
                      -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait earlyprintk"
                      -drive "file=$image,format=raw,if=none,id=hd0"
                      -device virtio-blk-device,drive=hd0)
            ;;
    esac
}
