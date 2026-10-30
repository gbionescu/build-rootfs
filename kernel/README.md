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

From the repository root, `./build_kernel.sh` wraps this script with the same
architecture names used by `build-rootfs.sh`:

```bash
./build_kernel.sh --arch arm64
```

Cross-compiling requires the matching toolchain:

```bash
sudo apt-get install gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf gcc-riscv64-linux-gnu
```

The script downloads and extracts `linux-6.1.22.tar.xz` on first use. The arm64,
arm32, and riscv64 configurations force virtio-mmio, virtio-blk, virtio-net, ext4,
devtmpfs, and the serial console driver to be built in so the resulting images boot
under QEMU `virt` machines with virtio-blk disks and user-mode networking.

DRM and virtio-gpu are also built in for the non-amd64 kernels, so
`run-qemu.sh --gui` and `--vnc` can provide a graphical display for Qt/Xorg
applications. `VIRTIO_INPUT` and `INPUT_EVDEV` are enabled so the virtio
keyboard and tablet show up as `/dev/input/event*` devices. The amd64 `.config`
already enables `CONFIG_DRM_VIRTIO_GPU`.

All kernels also enable `FTRACE`, `FUNCTION_TRACER`, `FUNCTION_GRAPH_TRACER`,
`FTRACE_SYSCALLS`, `KPROBES`, `KPROBE_EVENTS`, `UPROBES`, `UPROBE_EVENTS`,
`DYNAMIC_EVENTS`, `HIST_TRIGGERS`, `SYNTH_EVENTS`, `TRACER_SNAPSHOT`, and
`STACKTRACE` for the `linux-app/kernel-trace` examples. `HIST_TRIGGERS` is not
available on RISC-V configs without `ARCH_HAVE_NMI_SAFE_CMPXCHG`, so the hist and
synthetic examples skip there.

The pre-built kernel images tracked in this directory were not rebuilt with the
new trace options. Run `./build-kernel.sh <arch>` to rebuild before using
`hist`, `synthetic`, or `snapshot` in the guest.

9p (`NET_9P`, `NET_9P_VIRTIO`, `9P_FS`) is also built in for every architecture,
so the guest can mount QEMU 9p shares.

Running the top-level `./build_kernel.sh` accepts the same architecture names as
`build-rootfs.sh` (`amd64`, `arm64`, `arm32`, `riscv64`) and can copy the result
with `-o <path>`.
