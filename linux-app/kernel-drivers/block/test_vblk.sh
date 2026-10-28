#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
MODULE="$SCRIPT_DIR/vblk.ko"
MOUNT_DIR="/mnt/vblk-test"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

cleanup() {
    mountpoint -q "$MOUNT_DIR" && umount "$MOUNT_DIR" || true
    rmdir "$MOUNT_DIR" 2>/dev/null || true
    lsmod | grep -q '^vblk ' && rmmod vblk || true
}
trap cleanup EXIT

[[ $EUID -eq 0 ]] || fail "must run as root"
[[ -f "$MODULE" ]] || fail "vblk.ko not built; run ../build.sh arm64"

lsmod | grep -q '^vblk ' && rmmod vblk

insmod "$MODULE"
for _ in $(seq 1 50); do
    [[ -b /dev/vblk0 ]] && break
    sleep 0.1
done

if [[ ! -b /dev/vblk0 ]]; then
    dev="$(cat /sys/class/block/vblk0/dev 2>/dev/null || true)"
    if [[ -n "$dev" ]]; then
        mknod /dev/vblk0 b "${dev%:*}" "${dev#*:}"
    fi
fi
[[ -b /dev/vblk0 ]] || fail "/dev/vblk0 was not created"

mkfs.ext4 -F -q /dev/vblk0
mkdir -p "$MOUNT_DIR"
mount /dev/vblk0 "$MOUNT_DIR"

message="hello vblk $(date +%s)"
printf '%s\n' "$message" > "$MOUNT_DIR/hello.txt"
sync
result="$(cat "$MOUNT_DIR/hello.txt")"
[[ "$result" == "$message" ]] || fail "file content mismatch"

umount "$MOUNT_DIR"
rmdir "$MOUNT_DIR"
rmmod vblk
trap - EXIT

echo "vblk: PASS"
