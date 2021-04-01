# Alpine to image builder

This repo builds an `ext4` or an `initrd` based on an Alpine image.

Note: *some assembly required.*

## Building a rootfs

1. Customize `docker_rootfs_builder` to add custom list of packages.
Here you can install anything that you need in the final image.
2. Customize `alpine_boot` to run a startup script.
3. Run `./build_rootfs.sh`.

## Building an initrd

1. Customize `docker_rootfs_builder` to add custom list of packages.
Here you can install anything that you need in the final image.
2. Customize `alpine_boot` to run a startup script.
3. Run `./build_initrd.sh`.

## Utils

`ext4_to_initrd.sh rootfs.ext4` converts an ext4 rootfs to an initrd

## Troubleshooting

- Need a larger image? Change `build_rootfs.sh`.
- Need to run something at startup? Change `alpine_boot`