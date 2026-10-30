#!/bin/bash
set -u

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
PERF_BIN="$SCRIPT_DIR/perf_demo"
MODES=(stat group sample tracepoint kprobe uprobe pid cpu)
TRACE_DIR=""

PASS=0
SKIP=0
FAIL=0

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

find_tracefs() {
    if [[ -f /sys/kernel/tracing/available_tracers ]]; then
        TRACE_DIR=/sys/kernel/tracing
    elif [[ -f /sys/kernel/debug/tracing/available_tracers ]]; then
        TRACE_DIR=/sys/kernel/debug/tracing
    fi
}

ensure_tracefs() {
    find_tracefs
    if [[ -n "$TRACE_DIR" ]]; then
        return 0
    fi

    mkdir -p /sys/kernel/tracing 2>/dev/null || true
    mount -t tracefs nodev /sys/kernel/tracing 2>/dev/null || true
    find_tracefs
    [[ -n "$TRACE_DIR" ]]
}

run_mode() {
    local name="$1"
    local rc

    echo
    echo "=== $name ==="
    "$PERF_BIN" "$name"
    rc=$?

    case "$rc" in
        0)
            echo "[PASS] $name"
            PASS=$((PASS + 1))
            ;;
        2)
            echo "[SKIP] $name"
            SKIP=$((SKIP + 1))
            ;;
        *)
            echo "[FAIL] $name"
            FAIL=$((FAIL + 1))
            ;;
    esac
}

if [[ $EUID -ne 0 ]]; then
    fail "this script must be run as root (perf_event_open and tracefs)"
fi

ensure_tracefs || echo "WARN: tracefs is not available; tracepoint/kprobe/uprobe will skip" >&2

make -C "$SCRIPT_DIR" >/dev/null || fail "failed to build perf_demo"

case "${1:-all}" in
    all)
        for mode in "${MODES[@]}"; do
            run_mode "$mode"
        done
        ;;
    stat|group|sample|tracepoint|kprobe|uprobe|pid|cpu)
        run_mode "$1"
        ;;
    *)
        echo "Usage: $0 [all|stat|group|sample|tracepoint|kprobe|uprobe|pid|cpu]"
        exit 1
        ;;
esac

echo
echo "Passed: $PASS  Skipped: $SKIP  Failed: $FAIL"
[[ "$FAIL" -eq 0 ]]
