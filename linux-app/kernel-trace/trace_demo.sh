#!/bin/bash
set -u

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
TRACE_DIR=""

PASS=0
FAIL=0

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

tw() {
    echo "$2" > "$TRACE_DIR/$1" 2>/dev/null
}

trace_has() {
    grep -q "$1" "$TRACE_DIR/trace" 2>/dev/null
}

reset_trace() {
    tw tracing_on 0 || true
    tw current_tracer nop || true
    : > "$TRACE_DIR/trace" 2>/dev/null || true
}

cleanup() {
    if [[ -z "$TRACE_DIR" ]]; then
        return
    fi

    echo 0 > "$TRACE_DIR/tracing_on" 2>/dev/null || true
    echo nop > "$TRACE_DIR/current_tracer" 2>/dev/null || true
    echo > "$TRACE_DIR/set_graph_function" 2>/dev/null || true
    echo 0 > "$TRACE_DIR/events/sched/sched_switch/enable" 2>/dev/null || true
    echo 0 > "$TRACE_DIR/events/sched/sched_wakeup/enable" 2>/dev/null || true
    if [[ -e "$TRACE_DIR/events/kprobes/demo_vfs_read/enable" ]]; then
        echo 0 > "$TRACE_DIR/events/kprobes/demo_vfs_read/enable" 2>/dev/null || true
    fi
    echo '-:demo_vfs_read' > "$TRACE_DIR/kprobe_events" 2>/dev/null || true
}
trap cleanup EXIT

find_tracefs() {
    if [[ -f /sys/kernel/tracing/available_tracers ]]; then
        TRACE_DIR=/sys/kernel/tracing
    elif [[ -f /sys/kernel/debug/tracing/available_tracers ]]; then
        TRACE_DIR=/sys/kernel/debug/tracing
    else
        mkdir -p /sys/kernel/tracing 2>/dev/null || true
        mount -t tracefs nodev /sys/kernel/tracing 2>/dev/null || true
        if [[ -f /sys/kernel/tracing/available_tracers ]]; then
            TRACE_DIR=/sys/kernel/tracing
        fi
    fi
}

run_workload() {
    "$SCRIPT_DIR/trace_workload" > /tmp/trace-workload.log 2>&1 ||
        fail "trace_workload failed"
    grep -q TRACE_WORKLOAD_DONE /tmp/trace-workload.log ||
        fail "trace_workload did not finish"
}

show_sample() {
    local pattern="$1"

    echo "--- trace sample ($pattern) ---"
    grep -m 5 -A 6 "$pattern" "$TRACE_DIR/trace" | head -20
}

mode_function() {
    if ! grep -qw function_graph "$TRACE_DIR/available_tracers"; then
        echo "function_graph tracer is not available" >&2
        return 1
    fi
    if ! grep -qw vfs_read "$TRACE_DIR/available_filter_functions"; then
        echo "vfs_read is not traceable" >&2
        return 1
    fi

    reset_trace
    tw current_tracer function_graph || return 1
    tw set_graph_function vfs_read || return 1
    tw tracing_on 1 || return 1
    run_workload
    tw tracing_on 0

    if trace_has vfs_read; then
        show_sample vfs_read
        return 0
    fi
    return 1
}

mode_event() {
    reset_trace
    tw events/sched/sched_switch/enable 1 || return 1
    tw events/sched/sched_wakeup/enable 1 || return 1
    tw tracing_on 1 || return 1
    run_workload
    tw tracing_on 0
    tw events/sched/sched_switch/enable 0
    tw events/sched/sched_wakeup/enable 0

    if trace_has sched_switch; then
        show_sample sched_switch
        return 0
    fi
    return 1
}

mode_kprobe() {
    local rc=1

    echo > "$TRACE_DIR/kprobe_events" 2>/dev/null || true
    if ! echo 'p:demo_vfs_read vfs_read' > "$TRACE_DIR/kprobe_events" 2>/dev/null; then
        echo "failed to create kprobe on vfs_read" >&2
        return 1
    fi
    if [[ ! -e "$TRACE_DIR/events/kprobes/demo_vfs_read/enable" ]]; then
        echo "kprobe event directory was not created" >&2
        echo "kprobe_events: $(cat "$TRACE_DIR/kprobe_events" 2>/dev/null)" >&2
        return 1
    fi

    reset_trace
    if ! tw events/kprobes/demo_vfs_read/enable 1; then
        echo "failed to enable demo_vfs_read kprobe" >&2
        return 1
    fi
    tw tracing_on 1 || return 1
    run_workload
    tw tracing_on 0

    if trace_has demo_vfs_read; then
        show_sample demo_vfs_read
        rc=0
    fi

    tw events/kprobes/demo_vfs_read/enable 0
    echo '-:demo_vfs_read' > "$TRACE_DIR/kprobe_events" 2>/dev/null || true
    return "$rc"
}

run_mode() {
    local name="$1"
    local function="$2"

    echo
    echo "=== $name ==="
    if "$function"; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name"
        FAIL=$((FAIL + 1))
    fi
    reset_trace
}

if [[ $EUID -ne 0 ]]; then
    fail "this script must be run as root (tracefs access)"
fi

find_tracefs
if [[ -z "$TRACE_DIR" ]]; then
    fail "tracefs not available; rebuild the kernel with FTRACE/FUNCTION_TRACER"
fi

if [[ ! -x "$SCRIPT_DIR/trace_workload" ]]; then
    make -C "$SCRIPT_DIR" >/dev/null || fail "failed to build trace_workload"
fi

case "${1:-all}" in
    function)
        run_mode function mode_function
        ;;
    event)
        run_mode event mode_event
        ;;
    kprobe)
        run_mode kprobe mode_kprobe
        ;;
    all)
        run_mode function mode_function
        run_mode event mode_event
        run_mode kprobe mode_kprobe
        ;;
    *)
        echo "Usage: $0 [function|event|kprobe|all]"
        exit 1
        ;;
esac

echo
echo "Passed: $PASS  Failed: $FAIL"
[[ "$FAIL" -eq 0 ]]
