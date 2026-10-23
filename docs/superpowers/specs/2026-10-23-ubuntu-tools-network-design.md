# Ubuntu dev tools and QEMU networking

Date: 2026-10-23
Status: Approved design

## Goal

Make Ubuntu 22.04/24.04 rootfs images usable as small development guests: include
`git`, `ping`, `gcc`, `vim`, `apt`, `net-tools`, `sudo`, `ethtool`, `ssh`, and
`udhcpc`, and make QEMU user-mode networking work by default.

## Packages

Both Ubuntu type payloads install:

```text
git gcc libc6-dev vim sudo ethtool iputils-ping net-tools
openssh-server openssh-client udhcpc apt ca-certificates
iproute2 strace
```

`libc6-dev` is explicit so `gcc hello.c` works. `apt`, `net-tools`, and SSH were
already present but are listed explicitly. `vim` is the full package.

## Image size

Ubuntu `init-rootfs.sh` grows from 800M to 1536M to leave room for the toolchain,
editor runtime, and apt caches. The image remains sparse.

## QEMU network

`run-qemu.sh` enables QEMU user-mode networking by default:

- guest interface `eth0` via kernel argument `net.ifnames=0`
- `-netdev user,id=net0,hostfwd=tcp:127.0.0.1:<ssh-port>-:22`
- amd64: `virtio-net-pci`; arm64/arm32/riscv64: `virtio-net-device`
- default SSH forward port 2222; `--ssh-port <port>` overrides
- `--no-net` disables networking

The runner detects whether the QEMU binary from `PATH` has the `user` netdev
backend. If not, it tries `/usr/bin/<binary-name>`; this host has a custom
`/usr/local/bin` QEMU without slirp and an Ubuntu QEMU with slirp. If neither
supports `user`, the runner exits with an actionable error. This keeps the custom
QEMU for non-network boots while providing working networking.

## Guest DHCP and DNS

Both Ubuntu payloads:

- install `udhcpc`
- create and enable `/etc/systemd/system/udhcpc.service`:

```ini
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
```

- disable and mask `systemd-resolved`, because udhcpc's default script writes
  `/etc/resolv.conf`
- remove the `/sbin/resolvconf` compatibility symlink (`../bin/resolvectl`) so the
  udhcpc script takes the direct `resolv.conf` path

QEMU user-mode DHCP gives the guest `10.0.2.15/24`, gateway `10.0.2.2`, DNS
`10.0.2.3`.

## SSH

`sshd` is already enabled with `PermitRootLogin yes` and `root:root`. The runner
forwards host `127.0.0.1:2222` to guest port 22:

```bash
ssh -p 2222 root@127.0.0.1
```

## Tests

- `tests/test-arch.sh` verifies network arguments and the correct virtio-net
  device per architecture.
- `tests/qemu-test.sh` keeps network disabled by default to avoid host port
  conflicts.
- `tests/test-share.sh` gains `--no-net`.
- New `tests/test-tools.sh <image> <arch>` boots with network and the `linux-app`
  share, logs in, waits for DHCP, then checks all requested commands, compiles and
  runs `hello.c` from `/mnt/linux-app`, pings `10.0.2.2`, runs `apt-get update`,
  and checks that `ssh` is active.

## CI

- multiarch job timeout 240 minutes
- install `expect`
- after the multiarch build, run `tests/test-tools.sh` on the arm64 image

## Documentation

README documents:

- installed tools
- default networking, DHCP, and SSH forwarding
- `--no-net` and `--ssh-port`
- the QEMU fallback behavior

kernel/README notes `virtio-net` is built in.

## Acceptance criteria

1. `git`, `ping`, `gcc`, `vim`, `apt`, `netstat`, `sudo`, `ethtool`, `ssh`, and
   `udhcpc` exist in rebuilt Ubuntu images.
2. `gcc /mnt/linux-app/test1/hello.c` compiles and runs in the guest.
3. The guest gets an IP on `eth0` via udhcpc at boot.
4. `ping -c 1 10.0.2.2` succeeds and `apt-get update` succeeds.
5. `ssh -p 2222 root@127.0.0.1` reaches the guest.
6. `--no-net` still boots without a network device.
7. Existing share and boot tests still pass.

## Validation already performed

- `/usr/bin/qemu-system-aarch64` user backend boots the existing arm64 image with
  `net.ifnames=0`; the kernel reports `eth0` with `10.0.2.15`.
- SSH host forwarding to `127.0.0.1:2223` logged in as `root` and ran a command.
- A combined 9p share + user network boot read
  `/mnt/linux-app/test1/hello.c` over SSH, confirming share, auto-mount, network,
  and SSH work together.
