#!/bin/bash
# Build an initrd from alpine
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

"$SCRIPT_DIR"/build-rootfs.sh alpine
"$SCRIPT_DIR"/utils/ext4_to_initrd.sh rootfs.ext4
