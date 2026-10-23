# Multi-arch Ubuntu rootfs + QEMU Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Ubuntu 22.04/24.04 rootfs images for amd64, arm64, arm32, and riscv64, and boot every Ubuntu image with full-system QEMU.

**Architecture:** Reuse the existing Docker-based builder with Docker `--platform` plus qemu-user binfmt registration. A single `utils/arch.sh` owns the architecture mapping. Prebuilt 6.1.22 kernels are committed under `kernel/`, and a shared QEMU command builder is used by both the runner and the test harness.

**Tech Stack:** Bash, Docker, qemu-user/binfmt, qemu-system (x86_64/aarch64/arm/riscv64), Linux 6.1.22, GitHub Actions.

## Global Constraints

- Ubuntu 22.04 and 24.04 support amd64, arm64, arm32, riscv64. Alpine and Amazon Linux remain amd64-only.
- Canonical architecture names: `amd64`, `arm64`, `arm32`, `riscv64`.
- Aliases: `x86_64` → `amd64`, `aarch64` → `arm64`, `armhf`/`armv7`/`armv7l` → `arm32`.
- Default architecture is `amd64`.
- Default output is `rootfs.ext4` for amd64 and `rootfs_<arch>.ext4` for other architectures.
- Builds require root and Docker.
- Kernel version is 6.1.22; x86 output stays `kernel/bzImage-6.1.22`.
- New kernel outputs: `kernel/Image-6.1.22-arm64`, `kernel/zImage-6.1.22-arm32`, `kernel/Image-6.1.22-riscv64`.
- QEMU uses serial console only, no networking and no graphics.
- Emulated QEMU boot timeout is 300 seconds; amd64 stays at 60 seconds.
- Do not create git commits unless the user explicitly asks.

---

### Task 1: Architecture mapping utility

**Files:**
- Create: `utils/arch.sh`
- Test: `tests/test-arch.sh`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `VALID_ARCHS="amd64 arm64 arm32 riscv64"`
  - `KERNEL_DIR` (absolute path to `kernel/`)
  - `normalize_arch <name>` → prints canonical name, returns 1 on unknown
  - `type_supports_arch <type> <arch>` → returns 0/1
  - `set_arch_vars <arch>` → sets `ARCH`, `DOCKER_PLATFORM`, `QEMU_BIN`, `KERNEL_IMAGE`, `SERIAL_CONSOLE`, `ROOT_DEVICE`, `BINFMT_NAME`; returns 1 on unknown
  - `qemu_boot_command <image>` → sets array `QEMU_CMD`

- [ ] **Step 1: Write the failing architecture test**

Create `tests/test-arch.sh`:

```bash
#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
cd "$SCRIPT_DIR/.."
source "$SCRIPT_DIR/../utils/arch.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }

assert_eq() {
    local expected="$1" actual="$2" message="$3"
    if [[ "$expected" != "$actual" ]]; then
        fail "$message (expected '$expected', got '$actual')"
    fi
}

assert_eq "amd64" "$(normalize_arch x86_64)" "x86_64 alias"
assert_eq "amd64" "$(normalize_arch)" "empty alias"
assert_eq "arm64" "$(normalize_arch aarch64)" "aarch64 alias"
assert_eq "arm32" "$(normalize_arch armhf)" "armhf alias"
assert_eq "arm32" "$(normalize_arch armv7)" "armv7 alias"
assert_eq "riscv64" "$(normalize_arch riscv64)" "riscv64 name"
if normalize_arch sparc >/dev/null 2>&1; then
    fail "unknown architecture should fail"
fi

set_arch_vars amd64
assert_eq "linux/amd64" "$DOCKER_PLATFORM" "amd64 docker platform"
assert_eq "qemu-system-x86_64" "$QEMU_BIN" "amd64 qemu binary"
assert_eq "ttyS0" "$SERIAL_CONSOLE" "amd64 console"
assert_eq "/dev/sda" "$ROOT_DEVICE" "amd64 root device"
assert_eq "$KERNEL_DIR/bzImage-6.1.22" "$KERNEL_IMAGE" "amd64 kernel"

set_arch_vars arm64
assert_eq "linux/arm64/v8" "$DOCKER_PLATFORM" "arm64 docker platform"
assert_eq "qemu-system-aarch64" "$QEMU_BIN" "arm64 qemu binary"
assert_eq "ttyAMA0" "$SERIAL_CONSOLE" "arm64 console"
assert_eq "/dev/vda" "$ROOT_DEVICE" "arm64 root device"
assert_eq "$KERNEL_DIR/Image-6.1.22-arm64" "$KERNEL_IMAGE" "arm64 kernel"

set_arch_vars arm32
assert_eq "linux/arm/v7" "$DOCKER_PLATFORM" "arm32 docker platform"
assert_eq "qemu-system-arm" "$QEMU_BIN" "arm32 qemu binary"
assert_eq "ttyAMA0" "$SERIAL_CONSOLE" "arm32 console"
assert_eq "/dev/vda" "$ROOT_DEVICE" "arm32 root device"
assert_eq "$KERNEL_DIR/zImage-6.1.22-arm32" "$KERNEL_IMAGE" "arm32 kernel"

set_arch_vars riscv64
assert_eq "linux/riscv64" "$DOCKER_PLATFORM" "riscv64 docker platform"
assert_eq "qemu-system-riscv64" "$QEMU_BIN" "riscv64 qemu binary"
assert_eq "ttyS0" "$SERIAL_CONSOLE" "riscv64 console"
assert_eq "/dev/vda" "$ROOT_DEVICE" "riscv64 root device"
assert_eq "$KERNEL_DIR/Image-6.1.22-riscv64" "$KERNEL_IMAGE" "riscv64 kernel"

if type_supports_arch alpine arm64; then
    fail "alpine should not support arm64"
fi
if ! type_supports_arch ubuntu24.04 arm64; then
    fail "ubuntu24.04 should support arm64"
fi
if ! type_supports_arch ubuntu22.04 riscv64; then
    fail "ubuntu22.04 should support riscv64"
fi
if ! type_supports_arch alpine amd64; then
    fail "alpine should support amd64"
fi

set_arch_vars riscv64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" -bios default "*) ;;
    *) fail "riscv64 qemu command should use -bios default" ;;
esac
case " ${QEMU_CMD[*]} " in
    *" virtio-blk-device,drive=hd0 "*) ;;
    *) fail "riscv64 qemu command should use virtio-blk-device" ;;
esac

echo "All architecture tests passed"
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `chmod +x tests/test-arch.sh && ./tests/test-arch.sh`
Expected: FAIL because `utils/arch.sh` does not exist.

- [ ] **Step 3: Create `utils/arch.sh`**

```bash
#!/bin/bash
# Shared architecture definitions for rootfs builds and QEMU runs.

ARCH_UTILS_DIR="$( cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 ; pwd -P )"
KERNEL_DIR="$( cd "$ARCH_UTILS_DIR/.." >/dev/null 2>&1 ; pwd -P )/kernel"

VALID_ARCHS="amd64 arm64 arm32 riscv64"

normalize_arch() {
    case "$1" in
        ""|amd64|x86_64) echo "amd64" ;;
        arm64|aarch64) echo "arm64" ;;
        arm32|armhf|armv7|armv7l) echo "arm32" ;;
        riscv64) echo "riscv64" ;;
        *) return 1 ;;
    esac
}

type_supports_arch() {
    case "$1" in
        ubuntu22.04|ubuntu24.04) return 0 ;;
        *) [ "$2" = "amd64" ] ;;
    esac
}

set_arch_vars() {
    ARCH="$(normalize_arch "$1")" || return 1
    case "$ARCH" in
        amd64)
            DOCKER_PLATFORM="linux/amd64"
            QEMU_BIN="qemu-system-x86_64"
            KERNEL_IMAGE="$KERNEL_DIR/bzImage-6.1.22"
            SERIAL_CONSOLE="ttyS0"
            ROOT_DEVICE="/dev/sda"
            BINFMT_NAME="qemu-x86_64"
            ;;
        arm64)
            DOCKER_PLATFORM="linux/arm64/v8"
            QEMU_BIN="qemu-system-aarch64"
            KERNEL_IMAGE="$KERNEL_DIR/Image-6.1.22-arm64"
            SERIAL_CONSOLE="ttyAMA0"
            ROOT_DEVICE="/dev/vda"
            BINFMT_NAME="qemu-aarch64"
            ;;
        arm32)
            DOCKER_PLATFORM="linux/arm/v7"
            QEMU_BIN="qemu-system-arm"
            KERNEL_IMAGE="$KERNEL_DIR/zImage-6.1.22-arm32"
            SERIAL_CONSOLE="ttyAMA0"
            ROOT_DEVICE="/dev/vda"
            BINFMT_NAME="qemu-arm"
            ;;
        riscv64)
            DOCKER_PLATFORM="linux/riscv64"
            QEMU_BIN="qemu-system-riscv64"
            KERNEL_IMAGE="$KERNEL_DIR/Image-6.1.22-riscv64"
            SERIAL_CONSOLE="ttyS0"
            ROOT_DEVICE="/dev/vda"
            BINFMT_NAME="qemu-riscv64"
            ;;
    esac
}

qemu_boot_command() {
    local image="$1"
    case "$ARCH" in
        amd64)
            QEMU_CMD=("$QEMU_BIN" -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw earlyprintk=serial"
                      -nographic -m 1G -hda "$image")
            ;;
        arm64)
            QEMU_CMD=("$QEMU_BIN" -M virt -cpu cortex-a72 -m 1G -nographic
                      -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait earlyprintk"
                      -drive "file=$image,format=raw,if=none,id=hd0"
                      -device virtio-blk-device,drive=hd0)
            ;;
        arm32)
            QEMU_CMD=("$QEMU_BIN" -M virt -cpu cortex-a15 -m 1G -nographic
                      -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait earlyprintk"
                      -drive "file=$image,format=raw,if=none,id=hd0"
                      -device virtio-blk-device,drive=hd0)
            ;;
        riscv64)
            QEMU_CMD=("$QEMU_BIN" -M virt -m 1G -nographic -bios default
                      -kernel "$KERNEL_IMAGE"
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait earlyprintk"
                      -drive "file=$image,format=raw,if=none,id=hd0"
                      -device virtio-blk-device,drive=hd0)
            ;;
    esac
}
```

- [ ] **Step 4: Run the architecture test to verify it passes**

Run: `chmod +x utils/arch.sh && ./tests/test-arch.sh`
Expected: `All architecture tests passed`

- [ ] **Step 5: Verify shell syntax**

Run: `bash -n utils/arch.sh tests/test-arch.sh`
Expected: no output, exit status 0.

---

### Task 2: Multi-arch build CLI

**Files:**
- Modify: `build-rootfs.sh` (full rewrite)
- Test: `tests/test-arch.sh` (append CLI validation checks)

**Interfaces:**
- Consumes: `utils/arch.sh` functions from Task 1.
- Produces: `build-rootfs.sh <type> [--arch <arch>] [--output <path>] [--no-sparse]`, exporting `SPARSE`, `ARCH`, `DOCKER_PLATFORM`, `SERIAL_CONSOLE`, `ROOT_DEVICE`, `QEMU_BIN`, `KERNEL_IMAGE` to type scripts.

- [ ] **Step 1: Add failing CLI validation checks to `tests/test-arch.sh`**

Insert before the final `echo "All architecture tests passed"`:

```bash
output=$(./build-rootfs.sh alpine --arch arm64 2>&1 || true)
case "$output" in
    *"only supports amd64"*) ;;
    *) fail "alpine with arm64 should be rejected" ;;
esac

output=$(./build-rootfs.sh ubuntu24.04 --arch sparc 2>&1 || true)
case "$output" in
    *"Unknown architecture: sparc"*) ;;
    *) fail "unknown architecture should be rejected" ;;
esac

output=$(./build-rootfs.sh no-such-type 2>&1 || true)
case "$output" in
    *"is not a rootfs type"*) ;;
    *) fail "unknown rootfs type should be rejected" ;;
esac
```

- [ ] **Step 2: Run the test to verify the new checks fail**

Run: `./tests/test-arch.sh`
Expected: FAIL on the alpine/arm64 check because the current `build-rootfs.sh` does not validate architectures.

- [ ] **Step 3: Rewrite `build-rootfs.sh`**

```bash
#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$SCRIPT_DIR/utils/arch.sh"

usage() {
    cat <<EOF
Usage: $0 <rootfs-type> [options]

Options:
  -a, --arch <arch>     Target architecture: amd64 (default), arm64, arm32, riscv64
  -o, --output <path>   Output image path (default: rootfs.ext4 for amd64,
                        rootfs_<arch>.ext4 for other architectures)
  -s, --no-sparse       Do not create a sparse image
  -h, --help            Show this help

Examples:
  $0 ubuntu24.04
  $0 ubuntu24.04 --arch arm64
  $0 ubuntu22.04 --arch riscv64 -o /tmp/ubuntu-riscv64.ext4
EOF
}

TYPE=""
ARCH_INPUT="amd64"
OUTPUT=""
SPARSE=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -s|--no-sparse)
            SPARSE=0
            shift
            ;;
        -a|--arch)
            ARCH_INPUT="${2:?Missing value for $1}"
            shift 2
            ;;
        --arch=*)
            ARCH_INPUT="${1#*=}"
            shift
            ;;
        -o|--output)
            OUTPUT="${2:?Missing value for $1}"
            shift 2
            ;;
        --output=*)
            OUTPUT="${1#*=}"
            shift
            ;;
        -*)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
        *)
            if [[ -z "$TYPE" ]]; then
                TYPE="$1"
                shift
            else
                echo "Unexpected argument: $1"
                usage
                exit 1
            fi
            ;;
    esac
done

if [[ -z "$TYPE" ]]; then
    usage
    exit 1
fi

if [[ ! -d "$SCRIPT_DIR/type-$TYPE" ]]; then
    types=$(for d in "$SCRIPT_DIR"/type-*; do echo "${d##*/type-}"; done)
    printf '%s is not a rootfs type. Try one of:\n%s\n' "$TYPE" "$types"
    exit 1
fi

if ! set_arch_vars "$ARCH_INPUT"; then
    echo "Unknown architecture: $ARCH_INPUT. Try one of: $VALID_ARCHS"
    exit 1
fi

if ! type_supports_arch "$TYPE" "$ARCH"; then
    echo "Rootfs type '$TYPE' only supports amd64. Multi-arch types: ubuntu22.04, ubuntu24.04."
    exit 1
fi

if [[ -z "$OUTPUT" ]]; then
    if [[ "$ARCH" == "amd64" ]]; then
        RPATH="$SCRIPT_DIR/rootfs.ext4"
    else
        RPATH="$SCRIPT_DIR/rootfs_$ARCH.ext4"
    fi
else
    RPATH="$(realpath -m "$OUTPUT")"
fi

if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root"
    exit 1
fi

if [[ "$ARCH" != "amd64" && ! -f "/proc/sys/fs/binfmt_misc/$BINFMT_NAME" ]]; then
    echo "Registering binfmt handlers for $ARCH..."
    docker run --privileged --rm tonistiigi/binfmt --install arm,arm64,riscv64
    if [[ ! -f "/proc/sys/fs/binfmt_misc/$BINFMT_NAME" ]]; then
        echo "Could not register the binfmt handler for $ARCH."
        echo "Install qemu-user-static or enable binfmt support and retry."
        exit 1
    fi
fi

if [[ -f "$RPATH" ]]; then
    rm -f "$RPATH"
fi

export SPARSE
export ARCH
export DOCKER_PLATFORM
export SERIAL_CONSOLE
export ROOT_DEVICE
export QEMU_BIN
export KERNEL_IMAGE

"$SCRIPT_DIR/type-$TYPE/init-rootfs.sh" "$RPATH"
"$SCRIPT_DIR/type-$TYPE/run-container.sh" "$RPATH"
```

- [ ] **Step 4: Run the architecture test to verify it passes**

Run: `./tests/test-arch.sh`
Expected: `All architecture tests passed`

- [ ] **Step 5: Verify help output and shell syntax**

Run: `./build-rootfs.sh --help`
Expected: usage text that includes `--arch` and `--output`.

Run: `bash -n build-rootfs.sh`
Expected: no output, exit status 0.

---

### Task 3: Cross-arch Ubuntu type scripts

**Files:**
- Modify: `type-ubuntu22.04/run-container.sh`
- Modify: `type-ubuntu22.04/inside-container.sh`
- Modify: `type-ubuntu24.04/run-container.sh`
- Modify: `type-ubuntu24.04/inside-container.sh`

**Interfaces:**
- Consumes: `DOCKER_PLATFORM` and `SERIAL_CONSOLE` exported by `build-rootfs.sh`.
- Produces: architecture-correct Ubuntu rootfs in the ext4 image; services enabled offline.

- [ ] **Step 1: Verify offline service enablement works before changing the scripts**

Run:

```bash
docker run --rm ubuntu:24.04 bash -c \
  "apt-get update >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y systemd >/dev/null && systemctl enable ssh && systemctl enable getty@ttyS0"
```

Expected: two `Created symlink ...` lines and exit status 0. If this fails, stop and report the failure instead of continuing.

- [ ] **Step 2: Rewrite `type-ubuntu22.04/run-container.sh`**

```bash
#!/bin/bash
set -e
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

# Launch the target-architecture container
cont_id=$(docker run -td --rm --privileged --platform "$DOCKER_PLATFORM" -v "$1:/rootfs.ext4" ubuntu:22.04)

# Install systemd
docker exec -t "$cont_id" /bin/bash -c "apt-get update && DEBIAN_FRONTEND='noninteractive' apt-get install -y systemd"

# Run the payload in the already-running container. Services are enabled offline
# so this also works under qemu-user emulation.
set +e
docker exec -t -e SERIAL_CONSOLE="$SERIAL_CONSOLE" "$cont_id" /bin/sh -c "`cat "$SCRIPT_DIR/inside-container.sh"`"

rval=$?
set -e

docker stop "$cont_id"

if [[ "$rval" != 0 ]]; then
    echo "Error running the payload"
    exit 1
fi
```

- [ ] **Step 3: Rewrite `type-ubuntu22.04/inside-container.sh`**

```sh
#!/bin/sh
set -e
SERIAL_CONSOLE="${SERIAL_CONSOLE:-ttyS0}"

### Customize me!
DEBIAN_FRONTEND="noninteractive" apt-get install -y \
      openssh-server \
      openssh-client \
      iproute2 \
      net-tools strace

# Enable SSH
echo "PermitRootLogin yes" >> /etc/ssh/sshd_config
systemctl enable ssh

# Set password
echo "root:root" | chpasswd

# Enable the serial console; it starts on boot
systemctl enable "getty@${SERIAL_CONSOLE}"

# Then, copy the newly configured system to the rootfs image:
mkdir /my-rootfs
mount /rootfs.ext4 /my-rootfs

for d in bin etc lib lib64 root run sbin usr var; do
    if [ -e "/$d" ]; then
        tar c "/$d" | tar x -C /my-rootfs
    fi
done
for dir in dev proc run sys var tmp; do mkdir -p "/my-rootfs/${dir}"; done
umount /my-rootfs

# All done, exit docker shell
exit
```

- [ ] **Step 4: Rewrite `type-ubuntu24.04/run-container.sh`**

```bash
#!/bin/bash
set -e
SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

# Launch the target-architecture container
cont_id=$(docker run -td --rm --privileged --platform "$DOCKER_PLATFORM" -v "$1:/rootfs.ext4" ubuntu:24.04)

# Install systemd
docker exec -t "$cont_id" /bin/bash -c "apt-get update && DEBIAN_FRONTEND='noninteractive' apt-get install -y systemd"

# Run the payload in the already-running container. Services are enabled offline
# so this also works under qemu-user emulation.
set +e
docker exec -t -e SERIAL_CONSOLE="$SERIAL_CONSOLE" "$cont_id" /bin/sh -c "`cat "$SCRIPT_DIR/inside-container.sh"`"

rval=$?
set -e

docker stop "$cont_id"

if [[ "$rval" != 0 ]]; then
    echo "Error running the payload"
    exit 1
fi
```

- [ ] **Step 5: Rewrite `type-ubuntu24.04/inside-container.sh`**

Use the exact same content as `type-ubuntu22.04/inside-container.sh` from Step 3. Both files must be byte-identical:

```sh
#!/bin/sh
set -e
SERIAL_CONSOLE="${SERIAL_CONSOLE:-ttyS0}"

### Customize me!
DEBIAN_FRONTEND="noninteractive" apt-get install -y \
      openssh-server \
      openssh-client \
      iproute2 \
      net-tools strace

# Enable SSH
echo "PermitRootLogin yes" >> /etc/ssh/sshd_config
systemctl enable ssh

# Set password
echo "root:root" | chpasswd

# Enable the serial console; it starts on boot
systemctl enable "getty@${SERIAL_CONSOLE}"

# Then, copy the newly configured system to the rootfs image:
mkdir /my-rootfs
mount /rootfs.ext4 /my-rootfs

for d in bin etc lib lib64 root run sbin usr var; do
    if [ -e "/$d" ]; then
        tar c "/$d" | tar x -C /my-rootfs
    fi
done
for dir in dev proc run sys var tmp; do mkdir -p "/my-rootfs/${dir}"; done
umount /my-rootfs

# All done, exit docker shell
exit
```

- [ ] **Step 6: Verify shell syntax and identical Ubuntu payloads**

Run:

```bash
bash -n type-ubuntu22.04/run-container.sh type-ubuntu24.04/run-container.sh
sh -n type-ubuntu22.04/inside-container.sh type-ubuntu24.04/inside-container.sh
cmp type-ubuntu22.04/inside-container.sh type-ubuntu24.04/inside-container.sh
```

Expected: syntax checks print nothing; `cmp` prints nothing and exits 0.

---

### Task 4: Multi-arch kernel builder and kernel images

**Files:**
- Modify: `kernel/build-kernel.sh` (full rewrite)
- Modify: `.gitignore`
- Create: `kernel/.config-arm64`
- Create: `kernel/.config-arm32`
- Create: `kernel/.config-riscv64`
- Create: `kernel/Image-6.1.22-arm64`
- Create: `kernel/zImage-6.1.22-arm32`
- Create: `kernel/Image-6.1.22-riscv64`

**Interfaces:**
- Consumes: nothing.
- Produces: `./kernel/build-kernel.sh <arch>` writes the kernel image path consumed by `utils/arch.sh` (`KERNEL_IMAGE`).

- [ ] **Step 1: Verify cross compilers are installed**

Run: `aarch64-linux-gnu-gcc --version && arm-linux-gnueabihf-gcc --version && riscv64-linux-gnu-gcc --version`
Expected: each command prints a version line. If any is missing, install the matching `gcc-<arch>-linux-gnu` package before continuing.

- [ ] **Step 2: Rewrite `kernel/build-kernel.sh`**

```bash
#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
KERNEL_VERSION="6.1.22"
ARCH="${1:-amd64}"

case "$ARCH" in
    amd64)
        MAKE_ARCH=x86
        CROSS_COMPILE=""
        DEFCONFIG=""
        CONFIG_FILE="$SCRIPT_DIR/.config"
        KERNEL_PATH="arch/x86/boot/bzImage"
        OUTPUT="$SCRIPT_DIR/bzImage-$KERNEL_VERSION"
        EXTRA_CONFIG=()
        ;;
    arm64)
        MAKE_ARCH=arm64
        CROSS_COMPILE=aarch64-linux-gnu-
        DEFCONFIG=defconfig
        CONFIG_FILE="$SCRIPT_DIR/.config-arm64"
        KERNEL_PATH="arch/arm64/boot/Image"
        OUTPUT="$SCRIPT_DIR/Image-$KERNEL_VERSION-arm64"
        EXTRA_CONFIG=(
            VIRTIO VIRTIO_MMIO VIRTIO_BLK EXT4_FS
            DEVTMPFS DEVTMPFS_MOUNT
            SERIAL_AMBA_PL011 SERIAL_AMBA_PL011_CONSOLE
        )
        ;;
    arm32)
        MAKE_ARCH=arm
        CROSS_COMPILE=arm-linux-gnueabihf-
        DEFCONFIG=multi_v7_defconfig
        CONFIG_FILE="$SCRIPT_DIR/.config-arm32"
        KERNEL_PATH="arch/arm/boot/zImage"
        OUTPUT="$SCRIPT_DIR/zImage-$KERNEL_VERSION-arm32"
        EXTRA_CONFIG=(
            VIRTIO VIRTIO_MMIO VIRTIO_BLK EXT4_FS
            DEVTMPFS DEVTMPFS_MOUNT
            SERIAL_AMBA_PL011 SERIAL_AMBA_PL011_CONSOLE
        )
        ;;
    riscv64)
        MAKE_ARCH=riscv
        CROSS_COMPILE=riscv64-linux-gnu-
        DEFCONFIG=defconfig
        CONFIG_FILE="$SCRIPT_DIR/.config-riscv64"
        KERNEL_PATH="arch/riscv/boot/Image"
        OUTPUT="$SCRIPT_DIR/Image-$KERNEL_VERSION-riscv64"
        EXTRA_CONFIG=(
            VIRTIO VIRTIO_MMIO VIRTIO_BLK EXT4_FS
            DEVTMPFS DEVTMPFS_MOUNT
            SERIAL_8250 SERIAL_8250_CONSOLE SERIAL_8250_DW
        )
        ;;
    *)
        echo "Usage: $0 [amd64|arm64|arm32|riscv64]"
        exit 1
        ;;
esac

cd "$SCRIPT_DIR"

if [ ! -d "linux-$KERNEL_VERSION" ]; then
    wget "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KERNEL_VERSION.tar.xz"
    tar -xf "linux-$KERNEL_VERSION.tar.xz"
fi

cd "linux-$KERNEL_VERSION"

if [ "$ARCH" = "amd64" ]; then
    cp "$CONFIG_FILE" .config
else
    make ARCH="$MAKE_ARCH" CROSS_COMPILE="$CROSS_COMPILE" "$DEFCONFIG"
    for option in "${EXTRA_CONFIG[@]}"; do
        scripts/config --enable "$option"
    done
    make ARCH="$MAKE_ARCH" CROSS_COMPILE="$CROSS_COMPILE" olddefconfig
    cp .config "$CONFIG_FILE"
fi

make -j"$(nproc)" ARCH="$MAKE_ARCH" CROSS_COMPILE="$CROSS_COMPILE"
cp "$KERNEL_PATH" "$OUTPUT"
echo "Kernel written to $OUTPUT"
```

- [ ] **Step 3: Ignore downloaded kernel sources**

Update `.gitignore` to:

```gitignore
*.ext4
*.log
kernel/linux-*/
kernel/linux-*.tar.xz
```

Run: `git status --short`
Expected: the extracted kernel source and tarball are not listed as untracked.

- [ ] **Step 4: Build the arm64 kernel**

Run: `./kernel/build-kernel.sh arm64`
Expected: build completes and prints `Kernel written to .../kernel/Image-6.1.22-arm64`.

- [ ] **Step 5: Verify the arm64 kernel architecture**

Run: `file kernel/Image-6.1.22-arm64`
Expected: output contains `ARM aarch64` and `Linux kernel ARM64 boot executable Image`.

- [ ] **Step 6: Build the arm32 kernel**

Run: `./kernel/build-kernel.sh arm32`
Expected: build completes and prints `Kernel written to .../kernel/zImage-6.1.22-arm32`.

- [ ] **Step 7: Verify the arm32 kernel architecture**

Run: `file kernel/zImage-6.1.22-arm32`
Expected: output contains `ARM` and a `zImage`/`Linux kernel ARM boot executable zImage` description.

- [ ] **Step 8: Build the riscv64 kernel**

Run: `./kernel/build-kernel.sh riscv64`
Expected: build completes and prints `Kernel written to .../kernel/Image-6.1.22-riscv64`.

- [ ] **Step 9: Verify the riscv64 kernel architecture**

Run: `file kernel/Image-6.1.22-riscv64`
Expected: output contains `RISC-V` and `Linux kernel RISC-V boot executable Image`.

- [ ] **Step 10: Verify all kernel paths match the architecture mapping**

Run:

```bash
source utils/arch.sh
for arch in amd64 arm64 arm32 riscv64; do
    set_arch_vars "$arch"
    test -f "$KERNEL_IMAGE" || { echo "missing $KERNEL_IMAGE"; exit 1; }
    echo "$arch -> $KERNEL_IMAGE"
done
```

Expected: four lines, each pointing at an existing kernel file.

---

### Task 5: Multi-arch QEMU runner

**Files:**
- Modify: `run-qemu.sh` (full rewrite)

**Interfaces:**
- Consumes: `utils/arch.sh`, `KERNEL_IMAGE`, `QEMU_BIN`, `SERIAL_CONSOLE`, `ROOT_DEVICE`.
- Produces: `./run-qemu.sh <image> [--arch <arch>] [--kernel <path>]` that execs the correct QEMU binary.

- [ ] **Step 1: Rewrite `run-qemu.sh`**

```bash
#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$SCRIPT_DIR/utils/arch.sh"

usage() {
    cat <<EOF
Usage: $0 <rootfs-image> [options]

Options:
  -a, --arch <arch>    Architecture: amd64 (default), arm64, arm32, riscv64
  -k, --kernel <path>  Override the kernel image
  -h, --help           Show this help
EOF
}

IMAGE=""
ARCH_INPUT="amd64"
KERNEL_OVERRIDE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -a|--arch)
            ARCH_INPUT="${2:?Missing value for $1}"
            shift 2
            ;;
        --arch=*)
            ARCH_INPUT="${1#*=}"
            shift
            ;;
        -k|--kernel)
            KERNEL_OVERRIDE="${2:?Missing value for $1}"
            shift 2
            ;;
        --kernel=*)
            KERNEL_OVERRIDE="${1#*=}"
            shift
            ;;
        -*)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
        *)
            if [[ -z "$IMAGE" ]]; then
                IMAGE="$1"
                shift
            else
                echo "Unexpected argument: $1"
                usage
                exit 1
            fi
            ;;
    esac
done

if [[ -z "$IMAGE" ]]; then
    usage
    exit 1
fi

if ! set_arch_vars "$ARCH_INPUT"; then
    echo "Unknown architecture: $ARCH_INPUT. Try one of: $VALID_ARCHS"
    exit 1
fi

if [[ -n "$KERNEL_OVERRIDE" ]]; then
    KERNEL_IMAGE="$(realpath -m "$KERNEL_OVERRIDE")"
fi

if [[ ! -f "$IMAGE" ]]; then
    echo "Rootfs image not found: $IMAGE"
    exit 1
fi

if [[ ! -f "$KERNEL_IMAGE" ]]; then
    echo "Kernel not found: $KERNEL_IMAGE"
    echo "Build it with: ./kernel/build-kernel.sh $ARCH"
    exit 1
fi

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "QEMU binary not found: $QEMU_BIN"
    echo "Install the QEMU system emulation package for $ARCH."
    exit 1
fi

qemu_boot_command "$IMAGE"
exec "${QEMU_CMD[@]}"
```

- [ ] **Step 2: Verify help output and shell syntax**

Run: `./run-qemu.sh --help`
Expected: usage text containing `--arch` and `--kernel`.

Run: `bash -n run-qemu.sh`
Expected: no output, exit status 0.

- [ ] **Step 3: Verify the missing-image error path**

Run: `./run-qemu.sh /tmp/does-not-exist.ext4`
Expected: `Rootfs image not found: /tmp/does-not-exist.ext4`, exit status 1.

- [ ] **Step 4: Verify the missing-kernel error path**

Run: `touch /tmp/empty.ext4 && mv kernel/Image-6.1.22-riscv64 /tmp/Image-6.1.22-riscv64.bak; ./run-qemu.sh /tmp/empty.ext4 --arch riscv64; rval=$?; mv /tmp/Image-6.1.22-riscv64.bak kernel/Image-6.1.22-riscv64; exit $rval`
Expected: `Kernel not found: .../kernel/Image-6.1.22-riscv64` and `Build it with: ./kernel/build-kernel.sh riscv64`, exit status 1.

---

### Task 6: QEMU test harness and multiarch test suite

**Files:**
- Modify: `tests/qemu-test.sh` (full rewrite)
- Create: `tests/test-multiarch.sh`

**Interfaces:**
- Consumes: `utils/arch.sh`, kernel images from Task 4, `build-rootfs.sh` from Task 2.
- Produces:
  - `./tests/qemu-test.sh <image> [arch]` → exits 0 after seeing `login:`
  - `./tests/test-multiarch.sh` → builds and boots ubuntu24.04 on arm64, arm32, riscv64

- [ ] **Step 1: Rewrite `tests/qemu-test.sh`**

```bash
#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
source "$SCRIPT_DIR/../utils/arch.sh"

IMAGE="${1:?Usage: $0 <rootfs-image> [arch]}"
ARCH_INPUT="${2:-amd64}"

if ! set_arch_vars "$ARCH_INPUT"; then
    echo "Unknown architecture: $ARCH_INPUT. Try one of: $VALID_ARCHS"
    exit 1
fi

if [[ ! -f "$IMAGE" ]]; then
    echo "Rootfs image not found: $IMAGE"
    exit 1
fi

if [[ ! -f "$KERNEL_IMAGE" ]]; then
    echo "Kernel not found: $KERNEL_IMAGE"
    echo "Build it with: ./kernel/build-kernel.sh $ARCH"
    exit 1
fi

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    echo "QEMU binary not found: $QEMU_BIN"
    exit 1
fi

TIMEOUT=60
if [[ "$ARCH" != "amd64" ]]; then
    TIMEOUT=300
fi

LOG="serial_${ARCH}.log"
rm -f "$LOG"

echo "Testing $IMAGE ($ARCH)"
qemu_boot_command "$(realpath "$IMAGE")"
"${QEMU_CMD[@]}" > "$LOG" 2>&1 &
PID="$!"
trap 'kill -9 "$PID" 2>/dev/null || true' EXIT

timeout="$TIMEOUT"
result="PASS"
while ! grep -q "login:" "$LOG" 2>/dev/null; do
    echo "[$((TIMEOUT - timeout))/$TIMEOUT] Waiting... last line of $LOG: $(tail -n 1 "$LOG" 2>/dev/null)"

    sleep 1
    timeout=$((timeout - 1))
    if [[ "$timeout" -eq 0 ]]; then
        echo "Timeout waiting for QEMU ($ARCH) to start"
        result="FAIL"
        break
    fi
done

if [[ "$result" == "PASS" ]]; then
    echo "Found login prompt."
    rm -f "$LOG"
    echo "Test passed in $((TIMEOUT - timeout)) seconds"
else
    echo "Test failed; serial log kept at $LOG"
    exit 1
fi
```

- [ ] **Step 2: Create `tests/test-multiarch.sh`**

```bash
#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
cd "$SCRIPT_DIR/.."

if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root"
    exit 1
fi

for arch in arm64 arm32 riscv64; do
    echo "=== Building ubuntu24.04 for $arch ==="
    ./build-rootfs.sh ubuntu24.04 --arch "$arch"
    ./tests/qemu-test.sh "$(realpath "rootfs_$arch.ext4")" "$arch"
    rm -f "rootfs_ubuntu24.04_$arch.ext4"
    mv "rootfs_$arch.ext4" "rootfs_ubuntu24.04_$arch.ext4"
done

echo "All multiarch tests passed"
```

- [ ] **Step 3: Verify shell syntax and executable bits**

Run:

```bash
chmod +x tests/qemu-test.sh tests/test-multiarch.sh
bash -n tests/qemu-test.sh tests/test-multiarch.sh
```

Expected: no output, exit status 0.

- [ ] **Step 4: Verify the unknown-arch error path**

Run: `./tests/qemu-test.sh /tmp/empty.ext4 sparc`
Expected: `Unknown architecture: sparc. Try one of: amd64 arm64 arm32 riscv64`, exit status 1.

- [ ] **Step 5: Verify the missing-kernel error path**

Run: `touch /tmp/empty.ext4 && mv kernel/Image-6.1.22-arm64 /tmp/Image-6.1.22-arm64.bak; ./tests/qemu-test.sh /tmp/empty.ext4 arm64; rval=$?; mv /tmp/Image-6.1.22-arm64.bak kernel/Image-6.1.22-arm64; exit $rval`
Expected: `Kernel not found: .../kernel/Image-6.1.22-arm64` and `Build it with: ./kernel/build-kernel.sh arm64`, exit status 1.

---

### Task 7: CI workflow and documentation

**Files:**
- Create: `.github/workflows/multiarch.yml`
- Modify: `README.md`
- Modify: `kernel/README.md`

**Interfaces:**
- Consumes: `tests/test-multiarch.sh`, kernels, build CLI, QEMU runner.
- Produces: documented, CI-covered multiarch flow.

- [ ] **Step 1: Create `.github/workflows/multiarch.yml`**

```yaml
name: Multiarch CI

on:
  push:
    branches: [ master ]
  pull_request:
    branches: [ master ]
  workflow_dispatch:

jobs:
  multiarch:
    runs-on: ubuntu-latest
    timeout-minutes: 120

    steps:
      - uses: actions/checkout@v4

      - name: Install prerequisites
        run: |
          sudo apt-get update
          sudo apt-get install -y docker.io qemu-system-arm qemu-system-misc qemu-user-static

      - name: Register binfmt handlers
        run: docker run --privileged --rm tonistiigi/binfmt --install arm,arm64,riscv64

      - name: Build and test multiarch Ubuntu images
        run: sudo ./tests/test-multiarch.sh
```

- [ ] **Step 2: Update `README.md` prerequisites and build section**

Replace the existing `## Prerequisites` and `## Building a rootfs` sections with:

```markdown
## Prerequisites

Have `docker` installed.

Cross-architecture builds also require binfmt handlers. `build-rootfs.sh` registers
them automatically with Docker when they are missing:

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm,arm64,riscv64
```

## Building a rootfs

Simply run:

```bash
./build-rootfs.sh <rootfs-type>
```

Where `rootfs-type` is one of the root filesystem generators that starts with `type-`.
The default architecture is `amd64`.

For example, to generate an `alpine` rootfs, run:

```bash
./build-rootfs.sh alpine
```

Ubuntu 22.04 and 24.04 can be built for `arm64`, `arm32`, and `riscv64`:

```bash
./build-rootfs.sh ubuntu24.04 --arch arm64
./build-rootfs.sh ubuntu22.04 --arch arm32
./build-rootfs.sh ubuntu24.04 --arch riscv64
```

amd64 images are written to `rootfs.ext4`. Other architectures are written to
`rootfs_<arch>.ext4`. Use `-o <path>` to override the output path.

Notes:

- All images have ssh enabled by default.
- The default user is `root` with password `root`.
- By default, all images are generated as sparse files.
```

- [ ] **Step 3: Update the `## Run with QEMU` section in `README.md`**

Replace the existing section with:

```markdown
## Run with QEMU

To run a generated image with QEMU, run:

```bash
./run-qemu.sh rootfs.ext4
./run-qemu.sh rootfs_arm64.ext4 --arch arm64
./run-qemu.sh rootfs_arm32.ext4 --arch arm32
./run-qemu.sh rootfs_riscv64.ext4 --arch riscv64
```

This starts a QEMU instance with:
- no KVM, network, or graphics
- 1GB of RAM
- 1 CPU (default)
- serial console in the terminal
- a kernel image that is [built-in to the repo](kernel/README.md)

Emulated architectures boot much more slowly than amd64; allow several minutes for
the first boot.
```

- [ ] **Step 4: Update the `### Adding a new image type` and `### Tests` sections in `README.md`**

Replace the tests paragraph with:

```markdown
### Tests

`./tests/test-all.sh` builds all amd64 rootfs types and boots them with QEMU.

`./tests/test-multiarch.sh` builds Ubuntu 24.04 for arm64, arm32, and riscv64 and
boots each image. Emulated boots are slow, so this suite takes considerably longer.

`./tests/test-arch.sh` checks the architecture mapping and CLI validation without
building an image.

To run the amd64 tests, simply run:

```bash
./tests/test-all.sh
```
```

Keep the existing "Each rootfs generator consists of 3 files" text and the
"Adding a new image type" heading; only the tests subsection changes.

- [ ] **Step 5: Update `kernel/README.md`**

```markdown
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
```

- [ ] **Step 6: Validate the workflow YAML and Markdown references**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/multiarch.yml'))"`
Expected: no output, exit status 0.

Run: `grep -n "arch arm64\|arch arm32\|arch riscv64\|test-multiarch\|Image-6.1.22-arm64" README.md kernel/README.md`
Expected: matches in both files for all new architectures and the multiarch test suite.

---

### Task 8: End-to-end validation

**Files:**
- No source edits expected; this task verifies Tasks 1-7.

**Interfaces:**
- Consumes: all previous tasks.
- Produces: evidence that every acceptance criterion passes.

- [ ] **Step 1: Run the architecture and CLI tests**

Run: `./tests/test-arch.sh`
Expected: `All architecture tests passed`.

- [ ] **Step 2: Build ubuntu24.04 for arm64**

Run: `sudo ./build-rootfs.sh ubuntu24.04 --arch arm64`
Expected: `rootfs_arm64.ext4` exists and is non-empty.

- [ ] **Step 3: Verify the arm64 rootfs contains arm64 binaries**

Run:

```bash
mkdir -p /tmp/rootfs-check-arm64
sudo mount -o loop,ro rootfs_arm64.ext4 /tmp/rootfs-check-arm64
file /tmp/rootfs-check-arm64/bin/bash
sudo umount /tmp/rootfs-check-arm64
```

Expected: output contains `ARM aarch64`.

- [ ] **Step 4: Boot the arm64 image**

Run: `sudo ./tests/qemu-test.sh "$(realpath rootfs_arm64.ext4)" arm64`
Expected: `Found login prompt.` and `Test passed`.

- [ ] **Step 5: Build and boot the arm32 image**

Run:

```bash
sudo ./build-rootfs.sh ubuntu24.04 --arch arm32
mkdir -p /tmp/rootfs-check-arm32
sudo mount -o loop,ro rootfs_arm32.ext4 /tmp/rootfs-check-arm32
file /tmp/rootfs-check-arm32/bin/bash
sudo umount /tmp/rootfs-check-arm32
sudo ./tests/qemu-test.sh "$(realpath rootfs_arm32.ext4)" arm32
```

Expected: `file` reports `ARM, EABI5`; QEMU reports `Found login prompt.` and `Test passed`.

- [ ] **Step 6: Build and boot the riscv64 image**

Run:

```bash
sudo ./build-rootfs.sh ubuntu24.04 --arch riscv64
mkdir -p /tmp/rootfs-check-riscv64
sudo mount -o loop,ro rootfs_riscv64.ext4 /tmp/rootfs-check-riscv64
file /tmp/rootfs-check-riscv64/bin/bash
sudo umount /tmp/rootfs-check-riscv64
sudo ./tests/qemu-test.sh "$(realpath rootfs_riscv64.ext4)" riscv64
```

Expected: `file` reports `UCB RISC-V, RVC, double-float ABI`; QEMU reports
`Found login prompt.` and `Test passed`.

- [ ] **Step 7: Verify Ubuntu 22.04 also accepts the new architectures**

Run: `sudo ./build-rootfs.sh ubuntu22.04 --arch arm64`
Expected: `rootfs_arm64.ext4` is regenerated without errors.

- [ ] **Step 8: Verify the unsupported type/arch error**

Run: `./build-rootfs.sh alpine --arch arm64`
Expected: `Rootfs type 'alpine' only supports amd64. Multi-arch types: ubuntu22.04, ubuntu24.04.` and exit status 1.

- [ ] **Step 9: Verify the amd64 regression**

Run: `sudo ./tests/test-all.sh`
Expected: all four amd64 types build and pass QEMU, ending with `All tests passed`.

- [ ] **Step 10: Run the complete multiarch suite**

Run: `sudo ./tests/test-multiarch.sh`
Expected: all three architectures build and boot, ending with `All multiarch tests passed`.

- [ ] **Step 11: Review the final diff**

Run: `git status --short && git diff --stat`
Expected: only the files listed in Tasks 1-7 are modified/created, plus generated
`rootfs_*.ext4` files which are gitignored. Do not commit unless the user asks.
