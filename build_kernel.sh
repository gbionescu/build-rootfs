#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$SCRIPT_DIR/utils/arch.sh"

usage() {
    cat <<EOF
Usage: $0 [options]

Options:
  -a, --arch <arch>     Target architecture: amd64 (default), arm64, arm32, riscv64
  -o, --output <path>   Copy the built kernel image to this path
  -h, --help            Show this help

Examples:
  $0
  $0 --arch arm64
  $0 --arch riscv64 -o /tmp/Image-riscv64
EOF
}

ARCH_INPUT="amd64"
OUTPUT=""

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
            echo "Unexpected argument: $1"
            usage
            exit 1
            ;;
    esac
done

if ! set_arch_vars "$ARCH_INPUT"; then
    echo "Unknown architecture: $ARCH_INPUT. Try one of: $VALID_ARCHS"
    exit 1
fi

"$KERNEL_DIR/build-kernel.sh" "$ARCH"

if [[ -n "$OUTPUT" ]]; then
    OUTPUT="$(realpath -m "$OUTPUT")"
    if [[ "$OUTPUT" != "$KERNEL_IMAGE" ]]; then
        cp "$KERNEL_IMAGE" "$OUTPUT"
        echo "Kernel copied to $OUTPUT"
    fi
fi
