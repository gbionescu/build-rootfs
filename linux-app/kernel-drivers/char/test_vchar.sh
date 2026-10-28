#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
MODULE="$SCRIPT_DIR/vchar.ko"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[[ $EUID -eq 0 ]] || fail "must run as root"
[[ -f "$MODULE" ]] || fail "vchar.ko not built; run ../build.sh arm64"

lsmod | grep -q '^vchar ' && rmmod vchar

insmod "$MODULE"
for _ in $(seq 1 50); do
    [[ -c /dev/vchar ]] && break
    sleep 0.1
done

if [[ ! -c /dev/vchar ]]; then
    dev="$(cat /sys/class/misc/vchar/dev 2>/dev/null || true)"
    if [[ -n "$dev" ]]; then
        mknod /dev/vchar c "${dev%:*}" "${dev#*:}"
    fi
fi
[[ -c /dev/vchar ]] || fail "/dev/vchar was not created"

message="hello vchar $(date +%s)"
printf '%s' "$message" > /dev/vchar
result="$(cat /dev/vchar)"
[[ "$result" == "$message" ]] || fail "read back mismatch"

rmmod vchar
echo "vchar: PASS"
