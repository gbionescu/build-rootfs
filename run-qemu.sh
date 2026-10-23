#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$SCRIPT_DIR/utils/arch.sh"

usage() {
    cat <<EOF
Usage: $0 <rootfs-image> [options]

Options:
  -a, --arch <arch>    Architecture: amd64 (default), arm64, arm32, riscv64
  -k, --kernel <path>  Override the kernel image
  -s, --share <dir>    Share a host directory at /mnt/linux-app (default: ./linux-app)
      --no-share       Disable the default ./linux-app share
  -h, --help           Show this help
EOF
}

IMAGE=""
ARCH_INPUT="amd64"
KERNEL_OVERRIDE=""
SHARE_INPUT=""
NO_SHARE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -a|--arch)
            ARCH_INPUT="${2:?Missing value for $1}"
            shift 2
            ;;
        --arch=*)
            ARCH_INPUT="${1#*=}"
            shift
            ;;
        -k|--kernel)
            KERNEL_OVERRIDE="${2:?Missing value for $1}"
            shift 2
            ;;
        --kernel=*)
            KERNEL_OVERRIDE="${1#*=}"
            shift
            ;;
        -s|--share)
            SHARE_INPUT="${2:?Missing value for $1}"
            shift 2
            ;;
        --share=*)
            SHARE_INPUT="${1#*=}"
            shift
            ;;
        --no-share)
            NO_SHARE=1
            shift
            ;;
        -*)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
        *)
            if [[ -z "$IMAGE" ]]; then
                IMAGE="$1"
                shift
            else
                echo "Unexpected argument: $1"
                usage
                exit 1
            fi
            ;;
    esac
done

if [[ -z "$IMAGE" ]]; then
    usage
    exit 1
fi

if ! set_arch_vars "$ARCH_INPUT"; then
    echo "Unknown architecture: $ARCH_INPUT. Try one of: $VALID_ARCHS"
    exit 1
fi

if [[ -n "$KERNEL_OVERRIDE" ]]; then
    KERNEL_IMAGE="$(realpath -m "$KERNEL_OVERRIDE")"
fi

if [[ "$NO_SHARE" -eq 1 && -n "$SHARE_INPUT" ]]; then
    echo "Cannot use --share and --no-share together"
    exit 1
fi

if [[ "$NO_SHARE" -eq 1 ]]; then
    SHARE_DIR=""
elif [[ -n "$SHARE_INPUT" ]]; then
    SHARE_DIR="$(realpath -m "$SHARE_INPUT")"
elif [[ -d "$SCRIPT_DIR/linux-app" ]]; then
    SHARE_DIR="$SCRIPT_DIR/linux-app"
fi

if [[ -n "$SHARE_DIR" ]]; then
    if [[ ! -e "$SHARE_DIR" ]]; then
        echo "Share directory not found: $SHARE_DIR"
        exit 1
    fi
    if [[ ! -d "$SHARE_DIR" ]]; then
        echo "Share path is not a directory: $SHARE_DIR"
        exit 1
    fi
    if [[ "$SHARE_DIR" == *,* ]]; then
        echo "Share path must not contain commas: $SHARE_DIR"
        exit 1
    fi
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
    echo "Install the QEMU system emulation package for $ARCH."
    exit 1
fi

qemu_boot_command "$IMAGE"
exec "${QEMU_CMD[@]}"
