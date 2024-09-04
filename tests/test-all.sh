#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
cd "$SCRIPT_DIR/.."

# Check if root
if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root"
    exit 1
fi

do_test() {
    echo "Testing $1"
    sudo ./build-rootfs.sh "$1"

    cp rootfs.ext4 temp.ext4
    sudo ./tests/qemu-test.sh "$(realpath temp.ext4)"
    rm temp.ext4

    mv rootfs.ext4 "rootfs_$1.ext4"
}

do_test alpine
do_test amazonlinux2
do_test ubuntu22.04
do_test ubuntu24.04

echo "All tests passed"