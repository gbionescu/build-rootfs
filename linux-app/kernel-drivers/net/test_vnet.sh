#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
MODULE="$SCRIPT_DIR/vnet.ko"
NETNS="vnet-test"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

cleanup() {
    ip netns exec "$NETNS" ip link set vnet1 netns 1 2>/dev/null || true
    ip netns del "$NETNS" 2>/dev/null || true
    ip link set vnet0 down 2>/dev/null || true
    ip addr del 10.0.0.1/24 dev vnet0 2>/dev/null || true
    if ! ip link show vnet1 >/dev/null 2>&1; then
        # vnet1 may have been destroyed with the namespace; leave the module
        # loaded to avoid freeing a stale peer pointer.
        return
    fi
    lsmod | grep -q '^vnet ' && rmmod vnet || true
}
trap cleanup EXIT

[[ $EUID -eq 0 ]] || fail "must run as root"
[[ -f "$MODULE" ]] || fail "vnet.ko not built; run ../build.sh arm64"

lsmod | grep -q '^vnet ' && rmmod vnet

insmod "$MODULE"
for _ in $(seq 1 50); do
    ip link show vnet0 >/dev/null 2>&1 && ip link show vnet1 >/dev/null 2>&1 && break
    sleep 0.1
done
ip link show vnet0 >/dev/null 2>&1 || fail "vnet0 was not created"
ip link show vnet1 >/dev/null 2>&1 || fail "vnet1 was not created"

ip addr flush dev vnet0
ip addr add 10.0.0.1/24 dev vnet0
ip link set vnet0 up

ip netns del "$NETNS" 2>/dev/null || true
ip netns add "$NETNS"
ip link set vnet1 netns "$NETNS"
ip netns exec "$NETNS" ip link set lo up
ip netns exec "$NETNS" ip link set vnet1 up
ip netns exec "$NETNS" ip addr add 10.0.0.2/24 dev vnet1

ping -c 2 -W 2 10.0.0.2

ip netns exec "$NETNS" ip link set vnet1 netns 1
ip netns del "$NETNS"
ip link set vnet0 down
ip addr del 10.0.0.1/24 dev vnet0
rmmod vnet
trap - EXIT

echo "vnet: PASS"
