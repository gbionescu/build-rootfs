#!/bin/bash

###
# Build an initrd from a ext4 rootfs
###


set -e

this_path="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

"$this_path"/build_rootfs.sh
"$this_path"/ext4_to_initrd.sh rootfs.ext4
