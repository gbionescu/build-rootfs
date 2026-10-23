#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
REPO_DIR="$( cd "$SCRIPT_DIR/.." >/dev/null 2>&1 ; pwd -P )"

IMAGE="${1:?Usage: $0 <rootfs-image> <arch> [host-dir]}"
ARCH="${2:?Usage: $0 <rootfs-image> <arch> [host-dir]}"
SHARE_DIR="${3:-$REPO_DIR/linux-app}"

if [[ ! -f "$IMAGE" ]]; then
    echo "Rootfs image not found: $IMAGE"
    exit 1
fi
if [[ ! -d "$SHARE_DIR" ]]; then
    echo "Share directory not found: $SHARE_DIR"
    exit 1
fi
if ! command -v expect >/dev/null 2>&1; then
    echo "expect is required for this test"
    exit 1
fi

TMP_IMG="$(mktemp "/tmp/rootfs-share-${ARCH}-XXXXXX.ext4")"
cp --sparse=always "$IMAGE" "$TMP_IMG"
trap 'pkill -f "$TMP_IMG" 2>/dev/null || true; rm -f "$TMP_IMG"' EXIT

export RUN_QEMU="$REPO_DIR/run-qemu.sh"
export TEST_IMG="$TMP_IMG"
export TEST_ARCH="$ARCH"
export TEST_SHARE="$SHARE_DIR"

expect <<'EOF'
set timeout 300
log_user 1
spawn -noecho $env(RUN_QEMU) $env(TEST_IMG) --arch $env(TEST_ARCH) --share $env(TEST_SHARE) --no-net
expect {
    "login:" {}
    timeout { puts "TIMEOUT waiting for login"; exit 1 }
}
send "root\r"
expect {
    "Password:" {}
    timeout { puts "TIMEOUT waiting for password"; exit 1 }
}
send "root\r"
expect {
    "# " {}
    timeout { puts "TIMEOUT waiting for shell"; exit 1 }
}
send "test -f /mnt/linux-app/test1/hello.c && grep -q 'Hello World' /mnt/linux-app/test1/hello.c && echo SHARE_TEST_PASS\r"
expect {
    "SHARE_TEST_PASS" { puts "Share test passed" }
    timeout { puts "SHARE_TEST_FAIL"; exit 1 }
}
send "poweroff\r"
expect eof
EOF
