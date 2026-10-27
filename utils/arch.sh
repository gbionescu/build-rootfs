#!/bin/bash
# Shared architecture definitions for rootfs builds and QEMU runs.

ARCH_UTILS_DIR="$( cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 ; pwd -P )"
REPO_DIR="$( cd "$ARCH_UTILS_DIR/.." >/dev/null 2>&1 ; pwd -P )"
KERNEL_DIR="$REPO_DIR/kernel"

VALID_ARCHS="amd64 arm64 arm32 riscv64"

SHARE_DIR=""
SHARE_TAG="linuxapp"

NET_ENABLED=0
NET_BACKEND="user"
SSH_PORT=2222

DISPLAY_MODE="none"
VNC_DISPLAY=1

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

    if [[ "$ARCH" == "amd64" ]]; then
        ROOTFS_IMAGE="$REPO_DIR/rootfs.ext4"
    else
        ROOTFS_IMAGE="$REPO_DIR/rootfs_$ARCH.ext4"
    fi
}

qemu_display_args() {
    QEMU_DISPLAY_ARGS=()

    if [[ "$DISPLAY_MODE" == "none" ]]; then
        QEMU_DISPLAY_ARGS=(-nographic)
        return
    fi

    QEMU_DISPLAY_ARGS=(-serial mon:stdio)

    case "$DISPLAY_MODE" in
        sdl)
            QEMU_DISPLAY_ARGS+=(-display sdl)
            ;;
        vnc)
            QEMU_DISPLAY_ARGS+=(-vnc "127.0.0.1:$VNC_DISPLAY")
            ;;
    esac

    if [[ "$ARCH" == "amd64" ]]; then
        # x86 machines already provide a PS/2 keyboard and mouse.
        QEMU_DISPLAY_ARGS+=(-vga none -device virtio-gpu-pci)
    else
        # ARM/riscv virt machines have no default input devices.
        QEMU_DISPLAY_ARGS+=(-device virtio-gpu-pci
                            -device virtio-keyboard-pci
                            -device virtio-tablet-pci)
    fi
}

qemu_boot_command() {
    local image="$1"
    qemu_display_args

    case "$ARCH" in
        amd64)
            QEMU_CMD=("$QEMU_BIN" -kernel "$KERNEL_IMAGE"
                      "${QEMU_DISPLAY_ARGS[@]}"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw net.ifnames=0 earlyprintk=serial"
                      -m 1G -hda "$image")
            ;;
        arm64)
            QEMU_CMD=("$QEMU_BIN" -cpu max,sve=on,sve256=on -M virt,gic-version=3,its=on,iommu=smmuv3,mte=on -m 1G
                      "${QEMU_DISPLAY_ARGS[@]}"
                      -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait net.ifnames=0 earlyprintk"
                      -drive "file=$image,format=raw,if=none,id=hd0"
                      -device virtio-blk-device,drive=hd0)
            ;;
        arm32)
            QEMU_CMD=("$QEMU_BIN" -M virt -cpu cortex-a15 -m 1G
                      "${QEMU_DISPLAY_ARGS[@]}"
                      -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait net.ifnames=0 earlyprintk"
                      -drive "file=$image,format=raw,if=none,id=hd0"
                      -device virtio-blk-device,drive=hd0)
            ;;
        riscv64)
            QEMU_CMD=("$QEMU_BIN" -M virt -m 1G -bios default
                      "${QEMU_DISPLAY_ARGS[@]}"
                      -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait net.ifnames=0 earlyprintk"
                      -drive "file=$image,format=raw,if=none,id=hd0"
                      -device virtio-blk-device,drive=hd0)
            ;;
    esac

    if [[ -n "$SHARE_DIR" ]]; then
        QEMU_CMD+=(-fsdev "local,id=$SHARE_TAG,path=$SHARE_DIR,security_model=none")
        if [[ "$ARCH" == "amd64" ]]; then
            QEMU_CMD+=(-device "virtio-9p-pci,fsdev=$SHARE_TAG,mount_tag=$SHARE_TAG")
        else
            QEMU_CMD+=(-device "virtio-9p-device,fsdev=$SHARE_TAG,mount_tag=$SHARE_TAG")
        fi
    fi

    if [[ "$NET_ENABLED" -eq 1 ]]; then
        QEMU_CMD+=(-netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22")
        if [[ "$ARCH" == "amd64" ]]; then
            QEMU_CMD+=(-device "virtio-net-pci,netdev=net0")
        else
            QEMU_CMD+=(-device "virtio-net-device,netdev=net0")
        fi
    fi
}
