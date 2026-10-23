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

set_arch_vars arm64
assert_eq "linux/arm64/v8" "$DOCKER_PLATFORM" "arm64 docker platform"
assert_eq "qemu-system-aarch64" "$QEMU_BIN" "arm64 qemu binary"
assert_eq "ttyAMA0" "$SERIAL_CONSOLE" "arm64 console"
assert_eq "/dev/vda" "$ROOT_DEVICE" "arm64 root device"
assert_eq "$KERNEL_DIR/Image-6.1.22-arm64" "$KERNEL_IMAGE" "arm64 kernel"

set_arch_vars arm32
assert_eq "linux/arm/v7" "$DOCKER_PLATFORM" "arm32 docker platform"
assert_eq "qemu-system-arm" "$QEMU_BIN" "arm32 qemu binary"
assert_eq "ttyAMA0" "$SERIAL_CONSOLE" "arm32 console"
assert_eq "/dev/vda" "$ROOT_DEVICE" "arm32 root device"
assert_eq "$KERNEL_DIR/zImage-6.1.22-arm32" "$KERNEL_IMAGE" "arm32 kernel"

set_arch_vars riscv64
assert_eq "linux/riscv64" "$DOCKER_PLATFORM" "riscv64 docker platform"
assert_eq "qemu-system-riscv64" "$QEMU_BIN" "riscv64 qemu binary"
assert_eq "ttyS0" "$SERIAL_CONSOLE" "riscv64 console"
assert_eq "/dev/vda" "$ROOT_DEVICE" "riscv64 root device"
assert_eq "$KERNEL_DIR/Image-6.1.22-riscv64" "$KERNEL_IMAGE" "riscv64 kernel"

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

echo "All architecture tests passed"
