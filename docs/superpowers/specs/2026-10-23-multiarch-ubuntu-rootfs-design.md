# Multi-arch Ubuntu rootfs + QEMU support

Date: 2026-10-23
Status: Approved design

## Goal

Extend the existing Docker-based rootfs builder so Ubuntu 22.04 and 24.04 images can be
built for `arm64`, `arm32`, and `riscv64` in addition to the existing `amd64` images, and
so every produced Ubuntu image can be booted under full-system QEMU.

## Non-goals

- Alpine and Amazon Linux remain `amd64`-only.
- Initrd generation remains `amd64`/Alpine-only.
- No networking or graphics in the QEMU runner (serial console only), matching the
  current behavior.
- No release of the new images from CI yet.

## Architecture model

Canonical architecture names and aliases:

| Canonical | Aliases                  | Docker platform  | QEMU binary             | Kernel image                  | Serial console | Root device |
|-----------|--------------------------|------------------|-------------------------|-------------------------------|----------------|-------------|
| amd64     | x86_64                   | linux/amd64      | qemu-system-x86_64      | kernel/bzImage-6.1.22         | ttyS0          | /dev/sda    |
| arm64     | aarch64                  | linux/arm64/v8   | qemu-system-aarch64     | kernel/Image-6.1.22-arm64     | ttyAMA0        | /dev/vda    |
| arm32     | armhf, armv7             | linux/arm/v7     | qemu-system-arm         | kernel/zImage-6.1.22-arm32    | ttyAMA0        | /dev/vda    |
| riscv64   | (none)                   | linux/riscv64    | qemu-system-riscv64     | kernel/Image-6.1.22-riscv64   | ttyS0          | /dev/vda    |

A new `utils/arch.sh` is the single source of truth for this table. It exposes shell
functions/variables that `build-rootfs.sh`, `run-qemu.sh`, and the test scripts source.

## Build interface

`./build-rootfs.sh <type> [-a|--arch <arch>] [-o|--output <path>] [-s]`

- `--arch` defaults to `amd64`; aliases are normalized to canonical names.
- Output defaults:
  - `amd64`: `rootfs.ext4` (backwards compatible)
  - non-amd64: `rootfs_<arch>.ext4`
  - `--output` overrides both.
- `ubuntu22.04` and `ubuntu24.04` accept all architectures. Other types reject
  non-amd64 with a clear error listing supported types.
- For non-amd64 builds, the script verifies the required binfmt handler. If missing it
  registers handlers automatically through the standard
  `tonistiigi/binfmt` image (`docker run --privileged --rm tonistiigi/binfmt --install ...`),
  printing what it does. Builds already require root and Docker.
- Required environment exported to type scripts: `ARCH`, `DOCKER_PLATFORM`,
  `SERIAL_CONSOLE`, `ROOT_DEVICE`, `QEMU_BIN`, `KERNEL_IMAGE`.

## Type script changes

`type-ubuntu22.04/` and `type-ubuntu24.04/`:

- `init-rootfs.sh`: unchanged; the ext4 image format is architecture independent.
- `run-container.sh`: run the container with `--platform "$DOCKER_PLATFORM"` and pass
  `SERIAL_CONSOLE` to `inside-container.sh` via `docker exec -e`.
- `inside-container.sh`:
  - Install the same package set as today.
  - Enable SSH (`PermitRootLogin yes`, root password `root`) and explicitly enable the
    ssh service.
  - Enable `getty@${SERIAL_CONSOLE}` instead of hard-coding `ttyS0`.
  - Do not start services; service enablement must work without a running systemd PID 1,
    because systemd is not reliable under qemu-user emulation.
  - Copy only directories that exist (`lib64` is absent on armhf) so the copy loop works
    on every architecture.

## Kernel support

`kernel/build-kernel.sh` learns an architecture argument:

`./kernel/build-kernel.sh amd64|arm64|arm32|riscv64`

- amd64 keeps the existing 6.1.22 `.config` and `bzImage-6.1.22` output.
- arm64: `ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig`.
- arm32: `ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- multi_v7_defconfig`.
- riscv64: `ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig`.
- After defconfig, force the following built in (`=y`) with `scripts/config`:
  `VIRTIO`, `VIRTIO_MMIO`, `VIRTIO_BLK`, `EXT4_FS`, `DEVTMPFS`, `DEVTMPFS_MOUNT`,
  plus `SERIAL_AMBA_PL011`/`SERIAL_AMBA_PL011_CONSOLE` for ARM and `SERIAL_8250`/
  `SERIAL_8250_CONSOLE` for RISC-V.
- Outputs are committed alongside the existing x86 kernel:
  `Image-6.1.22-arm64`, `zImage-6.1.22-arm32`, `Image-6.1.22-riscv64`.
- `kernel/README.md` documents cross-compilation requirements and outputs.

## QEMU runner

`./run-qemu.sh <image> [--arch <arch>] [--kernel <path>]`

- Default arch is `amd64`; the existing `./run-qemu.sh rootfs.ext4` invocation keeps
  working unchanged.
- amd64 keeps the current command (`-hda`, `root=/dev/sda`).
- New architectures use:
  - `-M virt -m 1G -nographic`
  - `-drive file=<image>,format=raw,if=none,id=hd0`
  - `-device virtio-blk-device,drive=hd0`
  - kernel cmdline `console=<ttyAMA0|ttyS0> root=/dev/vda rw earlyprintk`
  - arm64: `-cpu cortex-a72`; arm32: `-cpu cortex-a15`; riscv64: `-bios default`
- `--kernel` overrides the per-arch kernel path.
- Missing QEMU binary or kernel produces a clear, actionable error.

## Testing

- `tests/qemu-test.sh <image> <arch>` boots the image and waits for `login:`.
  Timeout is 60s for amd64 and 300s for emulated architectures.
- `tests/test-multiarch.sh` builds `ubuntu24.04` for `arm64`, `arm32`, and `riscv64`,
  boots each image, and renames results after the test so builds do not overwrite each
  other.
- `tests/test-all.sh` remains the x86 suite; optionally accepts `amd64` for compatibility.
- `tests/qemu-test.sh` must fail clearly when the kernel or QEMU binary is absent.

## CI and documentation

- New `.github/workflows/multiarch.yml`, separate from the existing x86 workflow:
  installs Docker/QEMU/cross prerequisites, registers binfmt, runs
  `tests/test-multiarch.sh`. The x86 workflow and release asset list stay unchanged.
- README gains:
  - prerequisites for cross-arch builds (binfmt),
  - build examples per architecture,
  - QEMU examples per architecture,
  - notes about slow emulated boot times.
- `kernel/README.md` gains the cross-arch kernel build instructions.

## Error handling

- Unknown architecture or alias: list valid values.
- Unsupported type/arch pair: list which types support non-amd64.
- Missing binfmt handler that cannot be installed: instruct the user to install
  `qemu-user-static`/register binfmt.
- Missing kernel: point at `./kernel/build-kernel.sh <arch>`.
- Missing QEMU binary: point at the required package.
- QEMU boot timeout reports the architecture and last serial line.

## Acceptance criteria

1. `sudo ./build-rootfs.sh ubuntu24.04 --arch arm64` produces `rootfs_arm64.ext4`
   containing arm64 binaries.
2. The same works for `arm32` and `riscv64` with Ubuntu 22.04 and 24.04.
3. `sudo ./run-qemu.sh rootfs_arm64.ext4 --arch arm64` reaches a `login:` prompt.
   The same passes for arm32 and riscv64.
4. Existing amd64 builds and tests are unchanged in behavior and output naming.
5. `./build-rootfs.sh alpine --arch arm64` fails with an actionable message.
6. CI multiarch workflow builds and boots one Ubuntu 24.04 image per new architecture.
