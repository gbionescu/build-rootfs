#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$SCRIPT_DIR/utils/arch.sh"

usage() {
    cat <<EOF
Usage: $0 <rootfs-type> [options]

Options:
  -a, --arch <arch>     Target architecture: amd64 (default), arm64, arm32, riscv64
  -o, --output <path>   Output image path (default: rootfs.ext4 for amd64,
                        rootfs_<arch>.ext4 for other architectures)
  -s, --no-sparse       Do not create a sparse image
  -h, --help            Show this help

Examples:
  $0 ubuntu24.04
  $0 ubuntu24.04 --arch arm64
  $0 ubuntu22.04 --arch riscv64 -o /tmp/ubuntu-riscv64.ext4
EOF
}

TYPE=""
ARCH_INPUT="amd64"
OUTPUT=""
SPARSE=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -s|--no-sparse)
            SPARSE=0
            shift
            ;;
        -a|--arch)
            ARCH_INPUT="${2:?Missing value for $1}"
            shift 2
            ;;
        --arch=*)
            ARCH_INPUT="${1#*=}"
            shift
            ;;
        -o|--output)
            OUTPUT="${2:?Missing value for $1}"
            shift 2
            ;;
        --output=*)
            OUTPUT="${1#*=}"
            shift
            ;;
        -*)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
        *)
            if [[ -z "$TYPE" ]]; then
                TYPE="$1"
                shift
            else
                echo "Unexpected argument: $1"
                usage
                exit 1
            fi
            ;;
    esac
done

if [[ -z "$TYPE" ]]; then
    usage
    exit 1
fi

if [[ ! -d "$SCRIPT_DIR/type-$TYPE" ]]; then
    types=$(for d in "$SCRIPT_DIR"/type-*; do echo "${d##*/type-}"; done)
    printf '%s is not a rootfs type. Try one of:\n%s\n' "$TYPE" "$types"
    exit 1
fi

if ! set_arch_vars "$ARCH_INPUT"; then
    echo "Unknown architecture: $ARCH_INPUT. Try one of: $VALID_ARCHS"
    exit 1
fi

if ! type_supports_arch "$TYPE" "$ARCH"; then
    echo "Rootfs type '$TYPE' only supports amd64. Multi-arch types: ubuntu22.04, ubuntu24.04."
    exit 1
fi

if [[ -z "$OUTPUT" ]]; then
    if [[ "$ARCH" == "amd64" ]]; then
        RPATH="$SCRIPT_DIR/rootfs.ext4"
    else
        RPATH="$SCRIPT_DIR/rootfs_$ARCH.ext4"
    fi
else
    RPATH="$(realpath -m "$OUTPUT")"
fi

if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root"
    exit 1
fi

if [[ "$ARCH" != "amd64" && ! -f "/proc/sys/fs/binfmt_misc/$BINFMT_NAME" ]]; then
    echo "Registering binfmt handlers for $ARCH..."
    if ! docker run --privileged --rm tonistiigi/binfmt --install arm,arm64,riscv64; then
        echo "Could not register the binfmt handler for $ARCH."
        echo "Install qemu-user-static or enable binfmt support and retry."
        exit 1
    fi
    if [[ ! -f "/proc/sys/fs/binfmt_misc/$BINFMT_NAME" ]]; then
        echo "Could not register the binfmt handler for $ARCH."
        echo "Install qemu-user-static or enable binfmt support and retry."
        exit 1
    fi
fi

if [[ -f "$RPATH" ]]; then
    rm -f "$RPATH"
fi

export SPARSE
export ARCH
export DOCKER_PLATFORM
export SERIAL_CONSOLE
export ROOT_DEVICE
export QEMU_BIN
export KERNEL_IMAGE

"$SCRIPT_DIR/type-$TYPE/init-rootfs.sh" "$RPATH"
"$SCRIPT_DIR/type-$TYPE/run-container.sh" "$RPATH"
