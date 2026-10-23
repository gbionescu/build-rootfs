# QEMU linux-app Share Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Share the host `linux-app/` directory into QEMU guests over 9p and auto-mount it at `/mnt/linux-app` on amd64, arm64, arm32, and riscv64.

**Architecture:** `run-qemu.sh` resolves the share directory and exports it through `qemu_boot_command` in `utils/arch.sh`. Ubuntu images get an `/etc/fstab` automount entry. The arm32 kernel gains built-in 9p support. An expect-based test boots an image and verifies the shared file.

**Tech Stack:** Bash, QEMU 9p (`virtio-9p-pci`/`virtio-9p-device`), systemd automount, Linux 6.1.22, expect.

## Global Constraints

- Default share: `<repo>/linux-app` when it exists.
- `--share <dir>` overrides; `--no-share` disables.
- Mount tag: `linuxapp`; guest mount point: `/mnt/linux-app`.
- `security_model=none`; no host root required for QEMU.
- fstab options: `trans=virtio,version=9p2000.L,nofail,x-systemd.automount`.
- Paths containing commas are rejected.
- No CI changes and no virtiofs.
- Do not create git commits unless the user explicitly asks.

---

### Task 1: Kernel 9p support

**Files:**
- Modify: `kernel/build-kernel.sh`
- Modify: `kernel/.config-arm32`
- Modify: `kernel/zImage-6.1.22-arm32`

**Interfaces:**
- Consumes: nothing.
- Produces: arm32 kernel with built-in `NET_9P`, `NET_9P_VIRTIO`, `9P_FS`.

- [ ] **Step 1: Add 9p options to every non-amd64 config fragment**

In each of the `arm64`, `arm32`, and `riscv64` `EXTRA_CONFIG` arrays in
`kernel/build-kernel.sh`, append:

```bash
            NET_9P NET_9P_VIRTIO 9P_FS
```

The arm32 array becomes:

```bash
        EXTRA_CONFIG=(
            VIRTIO VIRTIO_MMIO VIRTIO_BLK EXT4_FS
            DEVTMPFS DEVTMPFS_MOUNT
            SERIAL_AMBA_PL011 SERIAL_AMBA_PL011_CONSOLE
            NET_9P NET_9P_VIRTIO 9P_FS
        )
```

- [ ] **Step 2: Rebuild the arm32 kernel**

Run: `./kernel/build-kernel.sh arm32`
Expected: `Kernel written to .../kernel/zImage-6.1.22-arm32`.

- [ ] **Step 3: Verify 9p is built in**

Run: `grep -E '^CONFIG_(NET_9P|NET_9P_VIRTIO|9P_FS)=' kernel/.config-arm32`
Expected:

```text
CONFIG_NET_9P=y
CONFIG_NET_9P_VIRTIO=y
CONFIG_9P_FS=y
```

- [ ] **Step 4: Verify arm64 and riscv64 still have 9p**

Run:

```bash
grep -E '^CONFIG_(NET_9P|NET_9P_VIRTIO|9P_FS)=' kernel/.config-arm64 kernel/.config-riscv64
```

Expected: each file reports `=y` for all three options.

---

### Task 2: QEMU share CLI and command generation

**Files:**
- Modify: `utils/arch.sh`
- Modify: `run-qemu.sh`
- Modify: `tests/test-arch.sh`

**Interfaces:**
- Consumes: `qemu_boot_command <image>` from Task 1 of the previous feature.
- Produces:
  - `SHARE_DIR` global (`""` disables sharing)
  - `SHARE_TAG="linuxapp"`
  - 9p arguments appended to `QEMU_CMD` when `SHARE_DIR` is non-empty
  - `run-qemu.sh --share/--no-share`

- [ ] **Step 1: Add failing command-generation assertions to `tests/test-arch.sh`**

Insert before the final `echo "All architecture tests passed"`:

```bash
SHARE_DIR=""
set_arch_vars amd64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" -fsdev "*) fail "amd64 without share should not add fsdev" ;;
esac

SHARE_DIR="/tmp/linux-app"
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-9p-pci,fsdev=linuxapp,mount_tag=linuxapp "*) ;;
    *) fail "amd64 share should use virtio-9p-pci" ;;
esac

set_arch_vars arm64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-9p-device,fsdev=linuxapp,mount_tag=linuxapp "*) ;;
    *) fail "arm64 share should use virtio-9p-device" ;;
esac

set_arch_vars arm32
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-9p-device,fsdev=linuxapp,mount_tag=linuxapp "*) ;;
    *) fail "arm32 share should use virtio-9p-device" ;;
esac

set_arch_vars riscv64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-9p-device,fsdev=linuxapp,mount_tag=linuxapp "*) ;;
    *) fail "riscv64 share should use virtio-9p-device" ;;
esac
SHARE_DIR=""
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `./tests/test-arch.sh`
Expected: FAIL with `amd64 share should use virtio-9p-pci`.

- [ ] **Step 3: Add share variables and append 9p args in `utils/arch.sh`**

At the top after `VALID_ARCHS`:

```bash
SHARE_DIR=""
SHARE_TAG="linuxapp"
```

At the end of `qemu_boot_command`, after the `case` block:

```bash
    if [[ -n "$SHARE_DIR" ]]; then
        QEMU_CMD+=(-fsdev "local,id=$SHARE_TAG,path=$SHARE_DIR,security_model=none")
        if [[ "$ARCH" == "amd64" ]]; then
            QEMU_CMD+=(-device "virtio-9p-pci,fsdev=$SHARE_TAG,mount_tag=$SHARE_TAG")
        else
            QEMU_CMD+=(-device "virtio-9p-device,fsdev=$SHARE_TAG,mount_tag=$SHARE_TAG")
        fi
    fi
```

- [ ] **Step 4: Rewrite the option parser in `run-qemu.sh`**

Add to the usage text:

```text
  -s, --share <dir>    Share a host directory at /mnt/linux-app (default: ./linux-app)
      --no-share       Disable the default ./linux-app share
```

Add parser cases:

```bash
        -s|--share)
            SHARE_INPUT="${2:?Missing value for $1}"
            shift 2
            ;;
        --share=*)
            SHARE_INPUT="${1#*=}"
            shift
            ;;
        --no-share)
            NO_SHARE=1
            shift
            ;;
```

Initialize before the loop:

```bash
SHARE_INPUT=""
NO_SHARE=0
```

After `set_arch_vars` and before the image check, add:

```bash
if [[ "$NO_SHARE" -eq 1 && -n "$SHARE_INPUT" ]]; then
    echo "Cannot use --share and --no-share together"
    exit 1
fi

if [[ "$NO_SHARE" -eq 1 ]]; then
    SHARE_DIR=""
elif [[ -n "$SHARE_INPUT" ]]; then
    SHARE_DIR="$(realpath -m "$SHARE_INPUT")"
elif [[ -d "$SCRIPT_DIR/linux-app" ]]; then
    SHARE_DIR="$SCRIPT_DIR/linux-app"
fi

if [[ -n "$SHARE_DIR" ]]; then
    if [[ ! -e "$SHARE_DIR" ]]; then
        echo "Share directory not found: $SHARE_DIR"
        exit 1
    fi
    if [[ ! -d "$SHARE_DIR" ]]; then
        echo "Share path is not a directory: $SHARE_DIR"
        exit 1
    fi
    if [[ "$SHARE_DIR" == *,* ]]; then
        echo "Share path must not contain commas: $SHARE_DIR"
        exit 1
    fi
fi
```

- [ ] **Step 5: Run the architecture test to verify it passes**

Run: `./tests/test-arch.sh`
Expected: `All architecture tests passed`.

- [ ] **Step 6: Verify CLI errors**

Run:

```bash
touch /tmp/empty.ext4
./run-qemu.sh /tmp/empty.ext4 --arch arm64 --share /no/such/dir
```

Expected: first prints `Share directory not found: /no/such/dir`; second prints
`Share path is not a directory: /tmp/empty.ext4`; both exit 1.

---

### Task 3: Guest auto-mount

**Files:**
- Modify: `type-ubuntu22.04/inside-container.sh`
- Modify: `type-ubuntu24.04/inside-container.sh`

**Interfaces:**
- Consumes: 9p mount tag `linuxapp` from Task 2.
- Produces: `/mnt/linux-app` and an fstab automount entry in built images.

- [ ] **Step 1: Add the mount point and fstab entry to both payloads**

Insert after the serial console block in both files:

```sh
mkdir -p /mnt/linux-app
if ! grep -q ' /mnt/linux-app ' /etc/fstab 2>/dev/null; then
    echo 'linuxapp /mnt/linux-app 9p trans=virtio,version=9p2000.L,nofail,x-systemd.automount 0 0' >> /etc/fstab
fi
```

- [ ] **Step 2: Verify both payloads are identical and syntactically valid**

Run:

```bash
cmp type-ubuntu22.04/inside-container.sh type-ubuntu24.04/inside-container.sh
sh -n type-ubuntu22.04/inside-container.sh type-ubuntu24.04/inside-container.sh
```

Expected: no output, exit status 0.

---

### Task 4: Expect-based share test

**Files:**
- Create: `tests/test-share.sh`

**Interfaces:**
- Consumes: `run-qemu.sh --share`, a built image with the Task 3 fstab entry.
- Produces: exit 0 and `Share test passed` when `/mnt/linux-app/test1/hello.c` is readable.

- [ ] **Step 1: Create `tests/test-share.sh`**

```bash
#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
REPO_DIR="$( cd "$SCRIPT_DIR/.." >/dev/null 2>&1 ; pwd -P )"

IMAGE="${1:?Usage: $0 <rootfs-image> <arch> [host-dir]}"
ARCH="${2:?Usage: $0 <rootfs-image> <arch> [host-dir]}"
SHARE_DIR="${3:-$REPO_DIR/linux-app}"

if [[ ! -f "$IMAGE" ]]; then
    echo "Rootfs image not found: $IMAGE"
    exit 1
fi
if [[ ! -d "$SHARE_DIR" ]]; then
    echo "Share directory not found: $SHARE_DIR"
    exit 1
fi
if ! command -v expect >/dev/null 2>&1; then
    echo "expect is required for this test"
    exit 1
fi

TMP_IMG="$(mktemp "/tmp/rootfs-share-${ARCH}-XXXXXX.ext4")"
cp --sparse=always "$IMAGE" "$TMP_IMG"
trap 'pkill -f "$TMP_IMG" 2>/dev/null || true; rm -f "$TMP_IMG"' EXIT

export RUN_QEMU="$REPO_DIR/run-qemu.sh"
export TEST_IMG="$TMP_IMG"
export TEST_ARCH="$ARCH"
export TEST_SHARE="$SHARE_DIR"

expect <<'EOF'
set timeout 300
log_user 1
spawn -noecho $env(RUN_QEMU) $env(TEST_IMG) --arch $env(TEST_ARCH) --share $env(TEST_SHARE)
expect {
    "login:" {}
    timeout { puts "TIMEOUT waiting for login"; exit 1 }
}
send "root\r"
expect {
    "Password:" {}
    timeout { puts "TIMEOUT waiting for password"; exit 1 }
}
send "root\r"
expect {
    "# " {}
    timeout { puts "TIMEOUT waiting for shell"; exit 1 }
}
send "test -f /mnt/linux-app/test1/hello.c && grep -q 'Hello World' /mnt/linux-app/test1/hello.c && echo SHARE_TEST_PASS\r"
expect {
    "SHARE_TEST_PASS" { puts "Share test passed" }
    timeout { puts "SHARE_TEST_FAIL"; exit 1 }
}
send "poweroff\r"
expect eof
EOF
```

- [ ] **Step 2: Verify syntax and executable bit**

Run: `chmod +x tests/test-share.sh && bash -n tests/test-share.sh`
Expected: no output, exit status 0.

- [ ] **Step 3: Verify the clear failure when expect is unavailable (optional)**

Run: `PATH=/nonexistent /bin/bash tests/test-share.sh /tmp/empty.ext4 arm64`
Expected: `expect is required for this test`, exit 1.

---

### Task 5: Documentation

**Files:**
- Modify: `README.md`
- Modify: `kernel/README.md`

- [ ] **Step 1: Add sharing details to `README.md`**

After the QEMU examples, add:

```markdown
By default, `./linux-app` is shared into the guest through 9p and auto-mounted at
`/mnt/linux-app`. Use `--share <dir>` to share another directory, or `--no-share`
to disable sharing:

```bash
./run-qemu.sh rootfs_arm64.ext4 --arch arm64
./run-qemu.sh rootfs_arm64.ext4 --arch arm64 --share /path/to/apps
./run-qemu.sh rootfs_arm64.ext4 --arch arm64 --no-share
```

Images built before the 9p auto-mount change must be rebuilt to get the
`/etc/fstab` entry.
```

In the Tests section, add:

```markdown
`./tests/test-share.sh <image> <arch>` boots an image with the default
`linux-app` share, logs in, and verifies `/mnt/linux-app/test1/hello.c`.
```

- [ ] **Step 2: Update `kernel/README.md`**

Append to the built-in driver sentence:

```text
9p (`NET_9P`, `NET_9P_VIRTIO`, `9P_FS`) is also built in for every architecture,
so the guest can mount QEMU 9p shares.
```

- [ ] **Step 3: Verify documentation matches the CLI**

Run: `grep -n -- '--share\|--no-share\|/mnt/linux-app\|test-share' README.md`
Expected: all four terms appear.

---

### Task 6: Validation

**Files:**
- No source edits expected.

- [ ] **Step 1: Run static checks**

Run:

```bash
./tests/test-arch.sh
bash -n run-qemu.sh utils/arch.sh kernel/build-kernel.sh tests/qemu-test.sh tests/test-share.sh tests/test-multiarch.sh
sh -n type-ubuntu22.04/inside-container.sh type-ubuntu24.04/inside-container.sh
git diff --check
```

Expected: `All architecture tests passed` and no syntax errors.

- [ ] **Step 2: Validate auto-mount on patched image copies**

For each architecture, copy the existing image, then use `debugfs -w` to create
`/mnt/linux-app` and add the fstab entry:

```bash
cp --sparse=always rootfs_ubuntu24.04_arm64.ext4 /tmp/test.img
printf 'linuxapp /mnt/linux-app 9p trans=virtio,version=9p2000.L,nofail,x-systemd.automount 0 0\n' > /tmp/linux-app-fstab
debugfs -w -R "mkdir /mnt" /tmp/test.img 2>/dev/null || true
debugfs -w -R "mkdir /mnt/linux-app" /tmp/test.img 2>/dev/null || true
debugfs -w -R "rm /etc/fstab" /tmp/test.img 2>/dev/null || true
debugfs -w -R "write /tmp/linux-app-fstab /etc/fstab" /tmp/test.img
```

Then run `tests/test-share.sh /tmp/test.img <arch>` for arm64, arm32, and riscv64.
Expected: `Share test passed` without any manual mount command; this also validates
9p in the rebuilt arm32 kernel.

- [ ] **Step 3: Verify no-share still boots**

Run: `./tests/qemu-test.sh rootfs_ubuntu24.04_arm64.ext4 arm64`
Expected: the test reaches `login:` and passes. `qemu-test.sh` does not enable a
share, so this exercises the `nofail` path for images that already contain fstab.

- [ ] **Step 4: Rebuild images after approval**

Run: `sudo ./tests/test-multiarch.sh`
Expected: all three images rebuild and pass the login test; the rebuilt images now
contain `/etc/fstab` and `/mnt/linux-app`.
