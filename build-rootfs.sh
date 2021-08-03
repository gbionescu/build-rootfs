#!/bin/bash
set -e
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
RPATH="$SCRIPT_DIR/rootfs.ext4"

usage() {
    echo "\
Usage: $0 [-t]
    -s generate sparse file - handling sparse files depends on the rootfs generator.
"
}

SPARSE=0 # 1 if creating sparse file

while getopts "h?s" opt; do
  case "$opt" in
    h|\?)
        usage
        exit 0
      ;;
    s)
        echo "Enabling sparse file creation."
        SPARSE=1
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
