#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
REPO_DIR="$( cd "$SCRIPT_DIR/../.." >/dev/null 2>&1 ; pwd -P )"
KERNEL_DIR="$REPO_DIR/kernel"
KERNEL_SRC="$KERNEL_DIR/linux-6.1.22"

COMMAND="build"
if [[ "${1:-}" == "clean" ]]; then
    COMMAND="clean"
    shift
fi

ARCH_INPUT="${1:-arm64}"

case "$ARCH_INPUT" in
    amd64)
        MAKE_ARCH=x86
        CROSS_COMPILE=""
        CONFIG_FILE="$KERNEL_DIR/.config"
        ;;
    arm64)
        MAKE_ARCH=arm64
        CROSS_COMPILE=aarch64-linux-gnu-
        CONFIG_FILE="$KERNEL_DIR/.config-arm64"
        ;;
    arm32)
        MAKE_ARCH=arm
        CROSS_COMPILE=arm-linux-gnueabihf-
        CONFIG_FILE="$KERNEL_DIR/.config-arm32"
        ;;
    riscv64)
        MAKE_ARCH=riscv
        CROSS_COMPILE=riscv64-linux-gnu-
        CONFIG_FILE="$KERNEL_DIR/.config-riscv64"
        ;;
    *)
        echo "Usage: $0 [amd64|arm64|arm32|riscv64]"
        echo "       $0 clean [arch]"
        exit 1
        ;;
esac

if [[ "$COMMAND" == "clean" ]]; then
    for dir in char block net; do
        rm -f "$SCRIPT_DIR/$dir"/*.ko "$SCRIPT_DIR/$dir"/*.o \
              "$SCRIPT_DIR/$dir"/*.mod "$SCRIPT_DIR/$dir"/*.mod.c \
              "$SCRIPT_DIR/$dir"/modules.order \
              "$SCRIPT_DIR/$dir"/Module.symvers \
              "$SCRIPT_DIR/$dir"/.*.cmd
    done
    echo "Cleaned kernel modules"
    exit 0
fi

if [[ ! -d "$KERNEL_SRC" ]]; then
    echo "Kernel source not found: $KERNEL_SRC"
    echo "Run ./kernel/build-kernel.sh $ARCH_INPUT first."
    exit 1
fi

cp "$CONFIG_FILE" "$KERNEL_SRC/.config"

make -C "$KERNEL_SRC" ARCH="$MAKE_ARCH" CROSS_COMPILE="$CROSS_COMPILE" olddefconfig
make -C "$KERNEL_SRC" ARCH="$MAKE_ARCH" CROSS_COMPILE="$CROSS_COMPILE" modules_prepare

for dir in char block net; do
    echo "==> building $dir"
    make -C "$KERNEL_SRC" ARCH="$MAKE_ARCH" CROSS_COMPILE="$CROSS_COMPILE" \
        M="$SCRIPT_DIR/$dir" KBUILD_MODPOST_WARN=1 modules
done

echo
echo "Built modules:"
ls -1 "$SCRIPT_DIR"/char/vchar.ko \
      "$SCRIPT_DIR"/block/vblk.ko \
      "$SCRIPT_DIR"/net/vnet.ko
