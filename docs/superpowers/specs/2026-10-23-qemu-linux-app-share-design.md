# QEMU linux-app 9p share + auto-mount

Date: 2026-10-23
Status: Approved design

## Goal

Let `run-qemu.sh` expose the local `linux-app/` directory to the guest through QEMU 9p
and auto-mount it at `/mnt/linux-app` on every supported architecture.

## Non-goals

- No virtiofs support.
- No networking, graphics, or SSH-based file transfer.
- No copy of `linux-app` into the rootfs image.
- No CI share test (emulated expect-based login would double CI runtime).

## Behavior

`./run-qemu.sh <image> [--arch <arch>] [--share <dir>] [--no-share]`

- Default share directory is `<repo>/linux-app` when that directory exists.
- `--share <dir>` overrides the default. The path must exist and be a directory.
- `--no-share` disables sharing even when `./linux-app` exists.
- Paths containing commas are rejected because QEMU's `-fsdev` option uses commas as
  separators.
- The QEMU mount tag is `linuxapp`.
- The guest mount point is `/mnt/linux-app`.

## Architecture mapping

| Architecture | 9p device |
|--------------|-----------|
| amd64 | `virtio-9p-pci` |
| arm64 | `virtio-9p-device` (MMIO) |
| arm32 | `virtio-9p-device` (MMIO) |
| riscv64 | `virtio-9p-device` (MMIO) |

The common QEMU fragment is:

```text
-fsdev local,id=linuxapp,path=<host-dir>,security_model=none
-device <virtio-9p-pci|virtio-9p-device>,fsdev=linuxapp,mount_tag=linuxapp
```

`security_model=none` keeps the share usable without host root privileges.

## Guest auto-mount

Both Ubuntu type payloads:

- create `/mnt/linux-app`
- append this line to `/etc/fstab`:

```text
linuxapp /mnt/linux-app 9p trans=virtio,version=9p2000.L,nofail,x-systemd.automount 0 0
```

`nofail` keeps boot working when QEMU does not provide a share. The automount unit
mounts the share on first access to `/mnt/linux-app`.

## Kernel support

9p is already built into the amd64, arm64, and riscv64 kernels. The arm32
`multi_v7_defconfig` lacks it, so the kernel builder adds:

```text
NET_9P NET_9P_VIRTIO 9P_FS
```

to every non-amd64 architecture fragment and rebuilds `zImage-6.1.22-arm32`.

## Testing

- `tests/test-arch.sh` verifies `qemu_boot_command` adds the 9p arguments only when a
  share directory is set, and uses the correct device type per architecture.
- New `tests/test-share.sh <image> <arch> [host-dir]` uses `expect` to boot the image,
  log in as `root`/`root`, access `/mnt/linux-app/test1/hello.c`, and report pass/fail.
  It uses `run-qemu.sh --share ...` so the runner path is exercised end to end.
- Manual verification is required after rebuilding images because the existing images do
  not contain the new fstab entry.

## Documentation

README adds:

- default `./linux-app` sharing
- `--share` / `--no-share` examples
- the guest mount point `/mnt/linux-app`
- a note that images built before this change must be rebuilt

kernel/README notes 9p is built in for every architecture.

## Error handling

- Missing share directory: `Share directory not found: <path>`.
- Path is a file: `Share path is not a directory: <path>`.
- Path contains a comma: reject with an explanation.
- Guest has no share: `nofail` prevents boot failure; accessing the mount point fails
  with a normal mount error.

## Acceptance criteria

1. `./run-qemu.sh rootfs_arm64.ext4 --arch arm64` shares `./linux-app` by default.
2. `--share /other/dir` and `--no-share` behave as described.
3. After rebuilding an Ubuntu image, `cat /mnt/linux-app/test1/hello.c` works inside the
   guest without a manual mount.
4. The same works for amd64, arm32, and riscv64.
5. No share present: the guest still boots.
6. Existing QEMU boot tests still pass.

## Validation already performed

On 2026-10-23, an arm64 guest booted from
`rootfs_ubuntu24.04_arm64.ext4` with a manual 9p mount of
`/home/zzz/code/mainline/build-rootfs/linux-app`; `ls /mnt/linux-app/test1` showed
`hello.c` and `cat` showed `printf("Hello World\n");`.
