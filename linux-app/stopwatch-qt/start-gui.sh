#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
BIN="$SCRIPT_DIR/stopwatch"

# Without a virtio-gpu device Xorg has no screen and prints "no screens found".
if [[ ! -e /dev/dri/card0 && ! -e /dev/fb0 ]]; then
    echo "No DRM/framebuffer device found (/dev/dri/card0 or /dev/fb0)."
    echo "Boot QEMU with a virtual GPU, for example:"
    echo "  ./run-qemu.sh --arch arm64 --gui"
    echo "  ./run-qemu.sh --arch arm64 --vnc 1"
    exit 1
fi

if [[ ! -x "$BIN" || "$SCRIPT_DIR/main.cpp" -nt "$BIN" ]]; then
    "$SCRIPT_DIR/build.sh"
fi

if ! command -v xinit >/dev/null 2>&1; then
    echo "xinit not found. Rebuild the Ubuntu rootfs with GUI dependencies."
    exit 1
fi

# Prefer the real server binary; this bypasses the Xorg.wrap console check when
# starting from the serial console.
XORG=/usr/lib/xorg/Xorg
if [[ ! -x "$XORG" ]]; then
    XORG=Xorg
fi

# Start openbox first so it can manage and focus the stopwatch window.
exec xinit /bin/sh -c "openbox >/tmp/openbox.log 2>&1 & sleep 2; exec $BIN" -- "$XORG" :0 vt1 -keeptty
