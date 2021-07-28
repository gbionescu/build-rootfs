#!/bin/bash
set -e
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
RPATH="$SCRIPT_DIR/rootfs.ext4"

if [ $UID != 0 ]; then
    echo "Needs root"
    exit 1
fi

if [[ ! -d "$SCRIPT_DIR/type-$1" ]]; then
    types=`ls -d1 ./type* | cut -d'-' -f2`
    echo -e "$1 is not a rootfs type. Try one of:\n$types"
fi

# Clean up old files
if [[ -f "$RPATH" ]]; then
    rm -rf "$RPATH"
fi

"type-$1/init-rootfs.sh" "$RPATH"
"type-$1/run-container.sh" "$RPATH"
