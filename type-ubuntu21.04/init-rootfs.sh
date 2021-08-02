#!/bin/bash

# Create the rootfs and format it
dd if=/dev/zero of="$1" bs=1M count=800 > /dev/null
yes | mkfs.ext4 "$1" > /dev/null
