# build-rootfs / kernel

This folder contains pre-built Linux 6.1.22 kernels:

| Architecture | File |
|--------------|------|
| amd64 | `bzImage-6.1.22` |
| arm64 | `Image-6.1.22-arm64` |
| arm32 | `zImage-6.1.22-arm32` |
| riscv64 | `Image-6.1.22-riscv64` |

To build a kernel image, run:

```bash
./build-kernel.sh amd64
./build-kernel.sh arm64
./build-kernel.sh arm32
./build-kernel.sh riscv64
```

Cross-compiling requires the matching toolchain:

```bash
sudo apt-get install gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf gcc-riscv64-linux-gnu
```

The script downloads and extracts `linux-6.1.22.tar.xz` on first use. The arm64,
arm32, and riscv64 configurations force virtio-mmio, virtio-blk, ext4, devtmpfs, and
the serial console driver to be built in so the resulting images boot under QEMU
`virt` machines with virtio-blk disks.
