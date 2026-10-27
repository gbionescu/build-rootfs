#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
REPO_DIR="$( cd "$SCRIPT_DIR/.." >/dev/null 2>&1 ; pwd -P )"

IMAGE="${1:?Usage: $0 <rootfs-image> <arch>}"
ARCH="${2:?Usage: $0 <rootfs-image> <arch>}"
SHARE_DIR="$REPO_DIR/linux-app"

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

TMP_IMG="$(mktemp "/tmp/rootfs-tools-${ARCH}-XXXXXX.ext4")"
cp --sparse=always "$IMAGE" "$TMP_IMG"
trap 'pkill -f "$TMP_IMG" 2>/dev/null || true; rm -f "$TMP_IMG"' EXIT

export RUN_QEMU="$REPO_DIR/run-qemu.sh"
export TEST_IMG="$TMP_IMG"
export TEST_ARCH="$ARCH"
export TEST_SHARE="$SHARE_DIR"

expect <<'EOF'
set timeout 600
log_user 1
spawn -noecho $env(RUN_QEMU) $env(TEST_IMG) --arch $env(TEST_ARCH) --share $env(TEST_SHARE)
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
send "for i in \$(seq 1 120); do ip -4 addr show eth0 2>/dev/null | grep -q 'inet ' && break; sleep 1; done; ok=1; for c in git ping gcc g++ make pkg-config vim apt-get netstat sudo ethtool ssh udhcpc; do command -v \$c >/dev/null || { echo MISSING:\$c; ok=0; }; done; gcc /mnt/linux-app/test1/hello.c -o /tmp/hello || ok=0; /tmp/hello | grep -q 'Hello World' || ok=0; ping -c 1 -W 2 10.0.2.2 >/dev/null || ok=0; apt-get update -qq || ok=0; systemctl is-active --quiet ssh || ok=0; \[ \"\$ok\" = 1 \] && echo TOOLS_PASS || echo TOOLS_FAIL\r"
expect {
    "TOOLS_PASS" { puts "Tools test passed" }
    timeout { puts "TOOLS_TEST_FAIL"; exit 1 }
}
send "poweroff\r"
expect eof
EOF
