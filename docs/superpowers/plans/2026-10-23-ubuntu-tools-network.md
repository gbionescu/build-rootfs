# Ubuntu Dev Tools and QEMU Networking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the requested development tools to Ubuntu images and make QEMU user-mode networking with DHCP and SSH work by default.

**Architecture:** Extend the Ubuntu payload package list and image size, add a udhcpc systemd service, extend `run-qemu.sh`/`qemu_boot_command` with user-mode networking and QEMU fallback, and validate with an expect-based tools test.

**Tech Stack:** Bash, Docker, systemd/udhcpc, QEMU user networking, virtio-net, expect, GitHub Actions.

## Global Constraints

- Ubuntu 22.04 and 24.04 only for the new package set.
- Packages: `git gcc libc6-dev vim sudo ethtool iputils-ping net-tools openssh-server openssh-client udhcpc apt ca-certificates iproute2 strace`.
- Ubuntu image size: 1536M, sparse by default.
- Guest interface: `eth0` via `net.ifnames=0`.
- Default SSH forward: `127.0.0.1:2222 -> guest:22`.
- `--no-net` disables networking; `--ssh-port <port>` overrides 2222.
- Guest DHCP service uses `/usr/sbin/udhcpc -f -i eth0 -R -s /etc/udhcpc/default.script`.
- `systemd-resolved` is disabled/masked and `/sbin/resolvconf` is removed so udhcpc writes `/etc/resolv.conf`.
- QEMU without a `user` netdev backend falls back to `/usr/bin/<binary-name>`.
- Do not create git commits unless the user explicitly asks.

---

### Task 1: Ubuntu packages and image size

**Files:**
- Modify: `type-ubuntu22.04/init-rootfs.sh`
- Modify: `type-ubuntu24.04/init-rootfs.sh`
- Modify: `type-ubuntu22.04/inside-container.sh`
- Modify: `type-ubuntu24.04/inside-container.sh`

**Interfaces:**
- Consumes: existing build flow.
- Produces: 1536M Ubuntu images with the requested tools and DHCP service.

- [ ] **Step 1: Increase the Ubuntu image size**

In both `init-rootfs.sh` files, replace `800M` with `1536M` and `800` with `1536`:

```bash
if [[ "$SPARSE" -eq 1 ]]; then
    dd if=/dev/zero of="$1" bs=1 count=0 seek=1536M > /dev/null
else
    dd if=/dev/zero of="$1" bs=1M count=1536 > /dev/null
fi
```

- [ ] **Step 2: Replace the package list in both payloads**

Replace the existing `apt-get install` block with:

```sh
DEBIAN_FRONTEND="noninteractive" apt-get install -y \
      git \
      gcc \
      libc6-dev \
      vim \
      sudo \
      ethtool \
      iputils-ping \
      net-tools \
      openssh-server \
      openssh-client \
      udhcpc \
      apt \
      ca-certificates \
      iproute2 \
      strace
```

- [ ] **Step 3: Add DHCP/DNS setup after the password block**

Insert:

```sh
# QEMU user-mode networking: let udhcpc own resolv.conf
systemctl disable systemd-resolved 2>/dev/null || true
systemctl mask systemd-resolved.service 2>/dev/null || true
rm -f /sbin/resolvconf

cat > /etc/systemd/system/udhcpc.service <<'EOF'
[Unit]
Description=udhcpc DHCP client on eth0
After=network.target
ConditionPathExists=/sys/class/net/eth0

[Service]
Type=simple
ExecStart=/usr/sbin/udhcpc -f -i eth0 -R -s /etc/udhcpc/default.script
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

systemctl enable udhcpc.service
```

- [ ] **Step 4: Verify both payloads are identical and valid**

Run:

```bash
cmp type-ubuntu22.04/inside-container.sh type-ubuntu24.04/inside-container.sh
sh -n type-ubuntu22.04/inside-container.sh type-ubuntu24.04/inside-container.sh
```

Expected: no output, exit status 0.

---

### Task 2: QEMU user-mode networking

**Files:**
- Modify: `utils/arch.sh`
- Modify: `run-qemu.sh`
- Modify: `tests/test-arch.sh`

**Interfaces:**
- Consumes: `qemu_boot_command <image>`.
- Produces:
  - `NET_ENABLED` (default 0), `NET_BACKEND="user"`, `SSH_PORT=2222`
  - network arguments in `QEMU_CMD` when `NET_ENABLED=1`
  - `run-qemu.sh --no-net`, `--ssh-port <port>`

- [ ] **Step 1: Add failing network assertions to `tests/test-arch.sh`**

Insert before the final `echo "All architecture tests passed"`:

```bash
NET_ENABLED=1
NET_BACKEND=user
SSH_PORT=2222
set_arch_vars amd64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22 "*) ;;
    *) fail "amd64 network should use user backend and hostfwd" ;;
esac
case " ${QEMU_CMD[*]} " in
    *" virtio-net-pci,netdev=net0 "*) ;;
    *) fail "amd64 network should use virtio-net-pci" ;;
esac

set_arch_vars arm64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-net-device,netdev=net0 "*) ;;
    *) fail "arm64 network should use virtio-net-device" ;;
esac

set_arch_vars arm32
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-net-device,netdev=net0 "*) ;;
    *) fail "arm32 network should use virtio-net-device" ;;
esac

set_arch_vars riscv64
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" virtio-net-device,netdev=net0 "*) ;;
    *) fail "riscv64 network should use virtio-net-device" ;;
esac

NET_ENABLED=0
qemu_boot_command /tmp/rootfs.ext4
case " ${QEMU_CMD[*]} " in
    *" -netdev "*) fail "network disabled should not add netdev" ;;
esac
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `./tests/test-arch.sh`
Expected: FAIL with `amd64 network should use user backend and hostfwd`.

- [ ] **Step 3: Add network variables and arguments in `utils/arch.sh`**

After the share variables:

```bash
NET_ENABLED=0
NET_BACKEND="user"
SSH_PORT=2222
```

Add `net.ifnames=0` to every `-append` string. For amd64:

```bash
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw net.ifnames=0 earlyprintk=serial"
```

For arm64/arm32:

```bash
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait net.ifnames=0 earlyprintk"
```

For riscv64:

```bash
                      -append "console=$SERIAL_CONSOLE root=$ROOT_DEVICE rw rootwait net.ifnames=0 earlyprintk"
```

After the 9p block at the end of `qemu_boot_command`:

```bash
    if [[ "$NET_ENABLED" -eq 1 ]]; then
        QEMU_CMD+=(-netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${SSH_PORT}-:22")
        if [[ "$ARCH" == "amd64" ]]; then
            QEMU_CMD+=(-device "virtio-net-pci,netdev=net0")
        else
            QEMU_CMD+=(-device "virtio-net-device,netdev=net0")
        fi
    fi
```

- [ ] **Step 4: Add the network CLI and QEMU fallback to `run-qemu.sh`**

Usage additions:

```text
      --no-net         Disable QEMU user-mode networking
      --ssh-port <p>   SSH host forward port on 127.0.0.1 (default: 2222)
```

Initialize before the parser:

```bash
NET_ENABLED=1
NET_BACKEND="user"
SSH_PORT=2222
```

Parser cases:

```bash
        --no-net)
            NET_ENABLED=0
            shift
            ;;
        --ssh-port)
            SSH_PORT="${2:?Missing value for $1}"
            shift 2
            ;;
        --ssh-port=*)
            SSH_PORT="${1#*=}"
            shift
            ;;
```

After the share validation and before the image check:

```bash
if ! [[ "$SSH_PORT" =~ ^[0-9]+$ ]] || (( SSH_PORT < 1 || SSH_PORT > 65535 )); then
    echo "Invalid SSH port: $SSH_PORT"
    exit 1
fi

if [[ "$NET_ENABLED" -eq 1 ]]; then
    if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
        echo "QEMU binary not found: $QEMU_BIN"
        exit 1
    fi

    qemu_has_user_net() {
        local bin="$1" machine
        case "$ARCH" in
            amd64) machine=pc ;;
            *) machine=virt ;;
        esac
        "$bin" -M "$machine" -netdev help 2>&1 | grep -qx "user"
    }

    if ! qemu_has_user_net "$QEMU_BIN"; then
        fallback_bin="/usr/bin/$(basename "$QEMU_BIN")"
        if [[ -x "$fallback_bin" && "$fallback_bin" != "$QEMU_BIN" ]] && qemu_has_user_net "$fallback_bin"; then
            echo "Using $fallback_bin for user-mode networking" >&2
            QEMU_BIN="$fallback_bin"
        else
            echo "QEMU user networking is not available: $QEMU_BIN"
            echo "Install a QEMU build with slirp support or use --no-net."
            exit 1
        fi
    fi
    NET_BACKEND=user
fi
```

- [ ] **Step 5: Run the architecture test to verify it passes**

Run: `./tests/test-arch.sh`
Expected: `All architecture tests passed`.

- [ ] **Step 6: Verify the CLI and fallback**

Run:

```bash
./run-qemu.sh --help | grep -E -- '--no-net|--ssh-port'
./run-qemu.sh /tmp/empty.ext4 --arch arm64 --ssh-port 99999
```

Expected: help lists both flags; the invalid port prints `Invalid SSH port: 99999`.
A normal run on this host prints `Using /usr/bin/qemu-system-aarch64 for user-mode networking`.

---

### Task 3: Tools test and test updates

**Files:**
- Create: `tests/test-tools.sh`
- Modify: `tests/test-share.sh`
- Modify: `tests/qemu-test.sh` (only if it changes `NET_ENABLED`; default is 0 so no edit is required)

**Interfaces:**
- Consumes: rebuilt image with Task 1 packages, `run-qemu.sh`.
- Produces: end-to-end tools/network test.

- [ ] **Step 1: Create `tests/test-tools.sh`**

```bash
#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
REPO_DIR="$( cd "$SCRIPT_DIR/.." >/dev/null 2>&1 ; pwd -P )"

IMAGE="${1:?Usage: $0 <rootfs-image> <arch>}"
ARCH="${2:?Usage: $0 <rootfs-image> <arch>}"
SHARE_DIR="$REPO_DIR/linux-app"

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

TMP_IMG="$(mktemp "/tmp/rootfs-tools-${ARCH}-XXXXXX.ext4")"
cp --sparse=always "$IMAGE" "$TMP_IMG"
trap 'pkill -f "$TMP_IMG" 2>/dev/null || true; rm -f "$TMP_IMG"' EXIT

export RUN_QEMU="$REPO_DIR/run-qemu.sh"
export TEST_IMG="$TMP_IMG"
export TEST_ARCH="$ARCH"
export TEST_SHARE="$SHARE_DIR"

expect <<'EOF'
set timeout 600
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
send "for i in \$(seq 1 120); do ip -4 addr show eth0 2>/dev/null | grep -q 'inet ' && break; sleep 1; done; ok=1; for c in git ping gcc vim apt-get netstat sudo ethtool ssh udhcpc; do command -v \$c >/dev/null || { echo MISSING:\$c; ok=0; }; done; gcc /mnt/linux-app/test1/hello.c -o /tmp/hello || ok=0; /tmp/hello | grep -q 'Hello World' || ok=0; ping -c 1 -W 2 10.0.2.2 >/dev/null || ok=0; apt-get update -qq || ok=0; systemctl is-active --quiet ssh || ok=0; [ \"\$ok\" = 1 ] && echo TOOLS_PASS || echo TOOLS_FAIL\r"
expect {
    "TOOLS_PASS" { puts "Tools test passed" }
    timeout { puts "TOOLS_TEST_FAIL"; exit 1 }
}
send "poweroff\r"
expect eof
EOF
```

- [ ] **Step 2: Make `test-share.sh` use `--no-net`**

Change the spawn line to:

```tcl
spawn -noecho $env(RUN_QEMU) $env(TEST_IMG) --arch $env(TEST_ARCH) --share $env(TEST_SHARE) --no-net
```

- [ ] **Step 3: Verify syntax and executable bits**

Run:

```bash
chmod +x tests/test-tools.sh
bash -n tests/test-tools.sh tests/test-share.sh
```

Expected: no output, exit status 0.

---

### Task 4: Documentation and CI

**Files:**
- Modify: `README.md`
- Modify: `kernel/README.md`
- Modify: `.github/workflows/multiarch.yml`

- [ ] **Step 1: Update the README build notes**

Add under the building notes:

```markdown
Ubuntu images include `git`, `gcc`, `vim`, `sudo`, `ethtool`, `ping`,
`net-tools`, OpenSSH, `udhcpc`, and `apt`, so they can be used as small
development guests.
```

- [ ] **Step 2: Update the QEMU section**

Change the options list to:

```markdown
This starts a QEMU instance with:
- no KVM or graphics
- 1GB of RAM
- 1 CPU (default)
- serial console in the terminal
- user-mode networking with DHCP on `eth0`
- SSH forwarded to `127.0.0.1:2222`
- a kernel image that is [built-in to the repo](kernel/README.md)
```

Add:

```markdown
The guest obtains `10.0.2.15` through `udhcpc`. Connect with:

```bash
ssh -p 2222 root@127.0.0.1
```

Use `--no-net` to disable networking and `--ssh-port <port>` to change the host
forward port.
```

Add to the Tests section:

```markdown
`./tests/test-tools.sh <image> <arch>` boots with networking and the `linux-app`
share, then checks the installed tools, compiles `hello.c`, pings `10.0.2.2`,
runs `apt-get update`, and verifies SSH is active.
```

- [ ] **Step 3: Update `kernel/README.md`**

Change the built-in driver sentence to include virtio-net:

```text
The arm64, arm32, and riscv64 configurations force virtio-mmio, virtio-blk,
virtio-net, ext4, devtmpfs, and the serial console driver to be built in.
```

- [ ] **Step 4: Update the multiarch workflow**

- change `timeout-minutes: 120` to `timeout-minutes: 240`
- add `expect` to the apt install list
- add after the multiarch test step:

```yaml
      - name: Test arm64 tools and networking
        run: sudo ./tests/test-tools.sh rootfs_ubuntu24.04_arm64.ext4 arm64
```

- [ ] **Step 5: Validate documentation and workflow syntax**

Run:

```bash
ruby -e "require 'yaml'; YAML.load_file('.github/workflows/multiarch.yml')"
grep -n -- '--no-net\|--ssh-port\|test-tools\|2222' README.md
```

Expected: YAML parses; README contains all terms.

---

### Task 5: Validation

- [ ] **Step 1: Run static checks**

```bash
./tests/test-arch.sh
bash -n run-qemu.sh utils/arch.sh tests/test-tools.sh tests/test-share.sh tests/qemu-test.sh
sh -n type-ubuntu22.04/inside-container.sh type-ubuntu24.04/inside-container.sh
git diff --check
```

Expected: architecture tests pass and no syntax errors.

- [ ] **Step 2: Verify the network command on this host without booting**

```bash
timeout 5 bash -x ./run-qemu.sh /tmp/test-combined.img --arch arm64 2>&1 | grep -E 'Using /usr/bin|netdev user|virtio-net-device'
```

Expected: fallback message and both network arguments appear.

- [ ] **Step 3: Rebuild images and run the tools test**

Run:

```bash
sudo ./tests/test-multiarch.sh
./tests/test-tools.sh rootfs_ubuntu24.04_arm64.ext4 arm64
```

Expected: multiarch boots pass; the tools test prints `Tools test passed`.
Repeat `test-tools.sh` for arm32 and riscv64 if time allows.

- [ ] **Step 4: Verify SSH from the host**

Boot an image with networking, then:

```bash
ssh -p 2222 root@127.0.0.1 'ip -4 addr show eth0; echo SSH_OK'
```

Expected: password `root` works and `SSH_OK` prints.

- [ ] **Step 5: Verify `--no-net`**

```bash
./tests/qemu-test.sh rootfs_ubuntu24.04_arm64.ext4 arm64
```

Expected: boot reaches `login:` with no network device and no host port conflict.
