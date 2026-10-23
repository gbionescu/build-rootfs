#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
cd "$SCRIPT_DIR/.."
source "$SCRIPT_DIR/../utils/arch.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }

assert_eq() {
    local expected="$1" actual="$2" message="$3"
    if [[ "$expected" != "$actual" ]]; then
        fail "$message (expected '$expected', got '$actual')"
    fi
}

assert_eq "amd64" "$(normalize_arch x86_64)" "x86_64 alias"
assert_eq "amd64" "$(normalize_arch)" "empty alias"
assert_eq "arm64" "$(normalize_arch aarch64)" "aarch64 alias"
assert_eq "arm32" "$(normalize_arch armhf)" "armhf alias"
assert_eq "arm32" "$(normalize_arch armv7)" "armv7 alias"
assert_eq "riscv64" "$(normalize_arch riscv64)" "riscv64 name"
if normalize_arch sparc >/dev/null 2>&1; then
    fail "unknown architecture should fail"
fi

set_arch_vars amd64
assert_eq "linux/amd64" "$DOCKER_PLATFORM" "amd64 docker platform"
assert_eq "qemu-system-x86_64" "$QEMU_BIN" "amd64 qemu binary"
assert_eq "ttyS0" "$SERIAL_CONSOLE" "amd64 console"
assert_eq "/dev/sda" "$ROOT_DEVICE" "amd64 root device"
assert_eq "$KERNEL_DIR/bzImage-6.1.22" "$KERNEL_IMAGE" "amd64 kernel"
assert_eq "$REPO_DIR/rootfs.ext4" "$ROOTFS_IMAGE" "amd64 rootfs"

set_arch_vars arm64
assert_eq "linux/arm64/v8" "$DOCKER_PLATFORM" "arm64 docker platform"
assert_eq "qemu-system-aarch64" "$QEMU_BIN" "arm64 qemu binary"
assert_eq "ttyAMA0" "$SERIAL_CONSOLE" "arm64 console"
assert_eq "/dev/vda" "$ROOT_DEVICE" "arm64 root device"
assert_eq "$KERNEL_DIR/Image-6.1.22-arm64" "$KERNEL_IMAGE" "arm64 kernel"
assert_eq "$REPO_DIR/rootfs_arm64.ext4" "$ROOTFS_IMAGE" "arm64 rootfs"

set_arch_vars arm32
assert_eq "linux/arm/v7" "$DOCKER_PLATFORM" "arm32 docker platform"
assert_eq "qemu-system-arm" "$QEMU_BIN" "arm32 qemu binary"
assert_eq "ttyAMA0" "$SERIAL_CONSOLE" "arm32 console"
assert_eq "/dev/vda" "$ROOT_DEVICE" "arm32 root device"
assert_eq "$KERNEL_DIR/zImage-6.1.22-arm32" "$KERNEL_IMAGE" "arm32 kernel"
assert_eq "$REPO_DIR/rootfs_arm32.ext4" "$ROOTFS_IMAGE" "arm32 rootfs"

set_arch_vars riscv64
assert_eq "linux/riscv64" "$DOCKER_PLATFORM" "riscv64 docker platform"
assert_eq "qemu-system-riscv64" "$QEMU_BIN" "riscv64 qemu binary"
assert_eq "ttyS0" "$SERIAL_CONSOLE" "riscv64 console"
assert_eq "/dev/vda" "$ROOT_DEVICE" "riscv64 root device"
assert_eq "$KERNEL_DIR/Image-6.1.22-riscv64" "$KERNEL_IMAGE" "riscv64 kernel"
assert_eq "$REPO_DIR/rootfs_riscv64.ext4" "$ROOTFS_IMAGE" "riscv64 rootfs"

if type_supports_arch alpine arm64; then
    fail "alpine should not support arm64"
fi
if ! type_supports_arch ubuntu24.04 arm64; then
    fail "ubuntu24.04 should support arm64"
fi
if ! type_supports_arch ubuntu22.04 riscv64; then
    fail "ubuntu22.04 should support riscv64"
fi
if ! type_supports_arch alpine amd64; then
    fail "alpine should support amd64"
fi

set_arch_vars riscv64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" -bios default "*) ;;
    *) fail "riscv64 qemu command should use -bios default" ;;
esac
case " ${QEMU_CMD[*]} " in
    *" virtio-blk-device,drive=hd0 "*) ;;
    *) fail "riscv64 qemu command should use virtio-blk-device" ;;
esac

output=$(./build-rootfs.sh alpine --arch arm64 2>&1 || true)
case "$output" in
    *"only supports amd64"*) ;;
    *) fail "alpine with arm64 should be rejected" ;;
esac

output=$(./build-rootfs.sh ubuntu24.04 --arch sparc 2>&1 || true)
case "$output" in
    *"Unknown architecture: sparc"*) ;;
    *) fail "unknown architecture should be rejected" ;;
esac

output=$(./build-rootfs.sh no-such-type 2>&1 || true)
case "$output" in
    *"is not a rootfs type"*) ;;
    *) fail "unknown rootfs type should be rejected" ;;
esac

output=$(./run-qemu.sh rootfs.ext4 --rootfs /tmp/other.ext4 2>&1 || true)
case "$output" in
    *"Cannot use a positional rootfs image together with --rootfs"*) ;;
    *) fail "positional rootfs and --rootfs should be rejected together" ;;
esac

missing_rootfs="/tmp/does-not-exist-$$.ext4"
output=$(./run-qemu.sh --rootfs "$missing_rootfs" --no-net 2>&1 || true)
case "$output" in
    *"Rootfs image not found: $missing_rootfs"*) ;;
    *) fail "--rootfs should override the rootfs image path" ;;
esac

SHARE_DIR=""
set_arch_vars amd64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" -fsdev "*) fail "amd64 without share should not add fsdev" ;;
esac

SHARE_DIR="/tmp/linux-app"
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-9p-pci,fsdev=linuxapp,mount_tag=linuxapp "*) ;;
    *) fail "amd64 share should use virtio-9p-pci" ;;
esac

set_arch_vars arm64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-9p-device,fsdev=linuxapp,mount_tag=linuxapp "*) ;;
    *) fail "arm64 share should use virtio-9p-device" ;;
esac

set_arch_vars arm32
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-9p-device,fsdev=linuxapp,mount_tag=linuxapp "*) ;;
    *) fail "arm32 share should use virtio-9p-device" ;;
esac

set_arch_vars riscv64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-9p-device,fsdev=linuxapp,mount_tag=linuxapp "*) ;;
    *) fail "riscv64 share should use virtio-9p-device" ;;
esac
SHARE_DIR=""

NET_ENABLED=1
NET_BACKEND=user
SSH_PORT=2222
set_arch_vars amd64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22 "*) ;;
    *) fail "amd64 network should use user backend and hostfwd" ;;
esac
case " ${QEMU_CMD[*]} " in
    *" virtio-net-pci,netdev=net0 "*) ;;
    *) fail "amd64 network should use virtio-net-pci" ;;
esac

set_arch_vars arm64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-net-device,netdev=net0 "*) ;;
    *) fail "arm64 network should use virtio-net-device" ;;
esac

set_arch_vars arm32
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-net-device,netdev=net0 "*) ;;
    *) fail "arm32 network should use virtio-net-device" ;;
esac

set_arch_vars riscv64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-net-device,netdev=net0 "*) ;;
    *) fail "riscv64 network should use virtio-net-device" ;;
esac

NET_ENABLED=0
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" -netdev "*) fail "network disabled should not add netdev" ;;
esac

echo "All architecture tests passed"
