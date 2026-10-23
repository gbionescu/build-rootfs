#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$SCRIPT_DIR/utils/arch.sh"

usage() {
    cat <<EOF
Usage: $0 [rootfs-image] [options]

The rootfs image can also be passed with --rootfs. When omitted, it defaults to
rootfs.ext4 for amd64 and rootfs_<arch>.ext4 for other architectures.

Options:
  -a, --arch <arch>    Architecture: amd64 (default), arm64, arm32, riscv64
  -r, --rootfs <path>  Override the rootfs image
  -k, --kernel <path>  Override the kernel image
  -s, --share <dir>    Share a host directory at /mnt/linux-app (default: ./linux-app)
      --no-share       Disable the default ./linux-app share
      --no-net         Disable QEMU user-mode networking
      --ssh-port <p>   SSH host forward port on 127.0.0.1 (default: 2222)
  -h, --help           Show this help
EOF
}

IMAGE=""
ARCH_INPUT="amd64"
ROOTFS_OVERRIDE=""
KERNEL_OVERRIDE=""
SHARE_INPUT=""
NO_SHARE=0
NET_ENABLED=1
NET_BACKEND="user"
SSH_PORT=2222

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
        -r|--rootfs)
            ROOTFS_OVERRIDE="${2:?Missing value for $1}"
            shift 2
            ;;
        --rootfs=*)
            ROOTFS_OVERRIDE="${1#*=}"
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
        --no-net)
            NET_ENABLED=0
            shift
            ;;
        --ssh-port)
            SSH_PORT="${2:?Missing value for $1}"
            shift 2
            ;;
        --ssh-port=*)
            SSH_PORT="${1#*=}"
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

if ! set_arch_vars "$ARCH_INPUT"; then
    echo "Unknown architecture: $ARCH_INPUT. Try one of: $VALID_ARCHS"
    exit 1
fi

if [[ -n "$ROOTFS_OVERRIDE" && -n "$IMAGE" ]]; then
    echo "Cannot use a positional rootfs image together with --rootfs"
    exit 1
fi

if [[ -n "$ROOTFS_OVERRIDE" ]]; then
    IMAGE="$ROOTFS_OVERRIDE"
elif [[ -z "$IMAGE" ]]; then
    IMAGE="$ROOTFS_IMAGE"
fi
IMAGE="$(realpath -m "$IMAGE")"

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

if ! [[ "$SSH_PORT" =~ ^[0-9]+$ ]] || (( SSH_PORT < 1 || SSH_PORT > 65535 )); then
    echo "Invalid SSH port: $SSH_PORT"
    exit 1
fi

if [[ "$NET_ENABLED" -eq 1 ]]; then
    if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
        echo "QEMU binary not found: $QEMU_BIN"
        exit 1
    fi

    qemu_has_user_net() {
        local bin="$1" machine
        case "$ARCH" in
            amd64) machine=pc ;;
            *) machine=virt ;;
        esac
        "$bin" -M "$machine" -netdev help 2>&1 | grep -qx "user"
    }

    if ! qemu_has_user_net "$QEMU_BIN"; then
        fallback_bin="/usr/bin/$(basename "$QEMU_BIN")"
        if [[ -x "$fallback_bin" && "$fallback_bin" != "$QEMU_BIN" ]] && qemu_has_user_net "$fallback_bin"; then
            echo "Using $fallback_bin for user-mode networking" >&2
            QEMU_BIN="$fallback_bin"
        else
            echo "QEMU user networking is not available: $QEMU_BIN"
            echo "Install a QEMU build with slirp support or use --no-net."
            exit 1
        fi
    fi
    NET_BACKEND=user
fi

if [[ ! -f "$IMAGE" ]]; then
    echo "Rootfs image not found: $IMAGE"
    echo "Specify one with --rootfs <path> or pass it as the first argument."
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
