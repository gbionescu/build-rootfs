#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$SCRIPT_DIR/../utils/arch.sh"

IMAGE="${1:?Usage: $0 <rootfs-image> [arch]}"
ARCH_INPUT="${2:-amd64}"

if ! set_arch_vars "$ARCH_INPUT"; then
    echo "Unknown architecture: $ARCH_INPUT. Try one of: $VALID_ARCHS"
    exit 1
fi

if [[ ! -f "$IMAGE" ]]; then
    echo "Rootfs image not found: $IMAGE"
    exit 1
fi

if [[ ! -f "$KERNEL_IMAGE" ]]; then
    echo "Kernel not found: $KERNEL_IMAGE"
    echo "Build it with: ./kernel/build-kernel.sh $ARCH"
    exit 1
fi

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "QEMU binary not found: $QEMU_BIN"
    exit 1
fi

TIMEOUT=60
if [[ "$ARCH" != "amd64" ]]; then
    TIMEOUT=300
fi

LOG="serial_${ARCH}.log"
rm -f "$LOG"

echo "Testing $IMAGE ($ARCH)"
qemu_boot_command "$(realpath "$IMAGE")"
"${QEMU_CMD[@]}" > "$LOG" 2>&1 &
PID="$!"
trap 'kill -9 "$PID" 2>/dev/null || true' EXIT

timeout="$TIMEOUT"
result="PASS"
while ! grep -q "login:" "$LOG" 2>/dev/null; do
    echo "[$((TIMEOUT - timeout))/$TIMEOUT] Waiting... last line of $LOG: $(tail -n 1 "$LOG" 2>/dev/null)"

    sleep 1
    timeout=$((timeout - 1))
    if ! kill -0 "$PID" 2>/dev/null; then
        echo "QEMU ($ARCH) exited before the login prompt"
        result="FAIL"
        break
    fi
    if [[ "$timeout" -eq 0 ]]; then
        echo "Timeout waiting for QEMU ($ARCH) to start"
        result="FAIL"
        break
    fi
done

if [[ "$result" == "PASS" ]]; then
    echo "Found login prompt."
    rm -f "$LOG"
    echo "Test passed in $((TIMEOUT - timeout)) seconds"
else
    echo "Test failed; serial log kept at $LOG"
    exit 1
fi
