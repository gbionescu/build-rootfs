# Rootfs image builder

This repo builds an `ext4` or an `initrd` using Docker containers.

Note: *some assembly required.*

## Prerequisites

Your user needs to be able to run `docker`.

If not, run scripts with sudo: `sudo build-rootfs.sh`.

## Building a rootfs

Simply run:

```bash
./build-rootfs.sh <rootfs-type>
```

Where `rootfs-type` is one of the root filesystem generators that starts with `type-`.

For example, to generate an `Ubuntu 21.04` rootfs, run:

```bash
./build-rootfs.sh ubuntu21.04
```

Notes:

- All images have ssh enabled by default.
- The default user is `root` with password `root`.
- By default, all images are generated as sparse files.

## Building an initrd

The initrd is built using `./build-initrd.sh`. By default it's based on the alpine image.

## Run with QEMU

To run the generated rootfs with QEMU, run:

```bash
./run-qemu.sh rootfs.ext4
```

This will start a QEMU instance with the rootfs with the following options:
- no KVM, network, or graphics
- 1GB of RAM
- 1 CPU (default)
- serial console in the terminal
- uses a kernel image that is [built-in to the repo](kernel/README.md)

## Customizing the build process

Each rootfs generator consists of 3 files:

- `init-rootfs.sh`: creates the root filesystem and formats it.
This is needed because a distribution may take a larger amount of space

- `inside-container.sh`: what is installed in a rootfs.
Here this script usually finishes with copying the rootfs contents to a mounted rootfs.ext4 file.

- `run-container.sh`: controls the container lifetime. For example the Ubuntu container is started
twice because systemd needs to be initialized.

### Adding a new image type

Just add a new folder with the 3 files described above

## Troubleshooting

- Need a larger image? Change `init-rootfs.sh`.

