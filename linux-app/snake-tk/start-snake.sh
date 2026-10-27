#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

if [[ ! -e /dev/dri/card0 && ! -e /dev/fb0 ]]; then
    echo "No DRM/framebuffer device found (/dev/dri/card0 or /dev/fb0)."
    echo "Boot QEMU with a virtual GPU, for example:"
    echo "  ./run-qemu.sh --arch arm64 --gui"
    echo "  ./run-qemu.sh --arch arm64 --vnc 1"
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is required."
    exit 1
fi

if ! python3 -c 'import tkinter' 2>/dev/null; then
    echo "tkinter is required (install python3-tk or rebuild the Ubuntu rootfs)."
    exit 1
fi

XORG=/usr/lib/xorg/Xorg
if [[ ! -x "$XORG" ]]; then
    XORG=Xorg
fi

# Start openbox first so it can manage and focus the game window.
exec xinit /bin/sh -c "openbox >/tmp/openbox.log 2>&1 & sleep 2; exec python3 $SCRIPT_DIR/snake.py" -- "$XORG" :0 vt1 -keeptty
