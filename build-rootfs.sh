#!/bin/bash
set -e

# Check if root
if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root"
    exit 1
fi

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
RPATH="$SCRIPT_DIR/rootfs.ext4"

usage() {
    echo "\
Usage: $0 [-t]
    -s don't generate a sparse file
"
}

SPARSE=1 # 1 if creating sparse file, 0 if not

while getopts "h?s" opt; do
  case "$opt" in
    h|\?)
        usage
        exit 0
      ;;
    s)
        echo "Disabled sparse file creation."
        SPARSE=0
        shift
      ;;
  esac
done

# Check if valid parameter
if [[ ! -d "$SCRIPT_DIR/type-$1" ]]; then
    types=`ls -d1 ./type* | cut -d'-' -f2`
    echo -e "$1 is not a rootfs type. Try one of:\n$types"
fi

# Clean up old files
if [[ -f "$RPATH" ]]; then
    rm -rf "$RPATH"
fi

export SPARSE="$SPARSE"

"type-$1/init-rootfs.sh" "$RPATH"
"type-$1/run-container.sh" "$RPATH"
