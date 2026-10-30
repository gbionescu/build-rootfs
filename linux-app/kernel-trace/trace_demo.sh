#!/bin/bash
set -u

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
WORKLOAD="$SCRIPT_DIR/trace_workload"
TRACE_DIR=""
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/trace-demo.XXXXXX")"
PIPE_PID=""

PASS=0
SKIP=0
FAIL=0

MODES=(function event syscall kprobe uprobe hist synthetic snapshot instance pipe stacktrace)

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

tw() {
    if [[ ! -e "$TRACE_DIR/$1" ]]; then
        return 1
    fi
    echo "$2" > "$TRACE_DIR/$1" 2>/dev/null
}

append() {
    if [[ ! -e "$TRACE_DIR/$1" ]]; then
        return 1
    fi
    echo "$2" >> "$TRACE_DIR/$1" 2>/dev/null
}

clear_synthetic() {
    append events/ftrace/print/trigger \
        '!hist:keys=common_pid:ts0=common_timestamp.usecs if buf == "start"' || true
    append events/ftrace/print/trigger \
        '!hist:keys=common_pid:lat=common_timestamp.usecs-$ts0:onmatch(ftrace.print).trace(demo_latency,$lat) if buf == "end"' || true
    append events/synthetic/demo_latency/trigger \
        '!hist:keys=lat,common_pid:sort=lat' || true
    tw events/synthetic/demo_latency/enable 0 || true
    append synthetic_events '!demo_latency u64 lat' || true
}

trace_has() {
    grep -q -- "$1" "$TRACE_DIR/trace" 2>/dev/null
}

show_sample() {
    local file="$1"
    local pattern="$2"

    echo "--- trace sample ($pattern) ---"
    grep -m 5 -A 6 -- "$pattern" "$file" 2>/dev/null | head -20
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

reset_trace() {
    [[ -z "$TRACE_DIR" ]] && return

    tw tracing_on 0 || true
    tw current_tracer nop || true
    tw set_graph_function "" || true
    tw max_graph_depth 0 || true
    tw options/funcgraph-duration 0 || true
    : > "$TRACE_DIR/trace" 2>/dev/null || true
}

cleanup() {
    if [[ -n "$PIPE_PID" ]]; then
        kill "$PIPE_PID" 2>/dev/null || true
        wait "$PIPE_PID" 2>/dev/null || true
        PIPE_PID=""
    fi

    if [[ -n "$TRACE_DIR" ]]; then
        tw tracing_on 0 || true
        tw current_tracer nop || true
        tw set_graph_function "" || true
        tw max_graph_depth 0 || true
        tw options/funcgraph-duration 0 || true

        for event in sched/sched_switch sched/sched_wakeup \
                     syscalls/sys_enter_getpid kmem/kmalloc; do
            tw "events/$event/enable" 0 || true
            tw "events/$event/filter" 0 || true
            tw "events/$event/trigger" "" || true
        done

        tw events/ftrace/print/filter 0 || true
        append events/sched/sched_wakeup/trigger \
            '!hist:keys=comm:vals=hitcount' || true
        append events/sched/sched_switch/trigger '!stacktrace' || true
        append events/kmem/kmalloc/trigger '!stacktrace' || true
        clear_synthetic

        tw kprobe_events '-:demo_vfs_read' || true
        tw kprobe_events '-:demo_vfs_read_ret' || true
        tw uprobe_events '-:demo_uprobe' || true
        tw uprobe_events '-:demo_uprobe_ret' || true

        rmdir "$TRACE_DIR/instances/demo" 2>/dev/null || true
        : > "$TRACE_DIR/trace" 2>/dev/null || true
    fi

    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

run_workload() {
    "$WORKLOAD" "$@" >/dev/null 2>&1
}

mode_function() {
    if ! grep -qw function_graph "$TRACE_DIR/available_tracers"; then
        echo "function_graph tracer is not available" >&2
        return 2
    fi
    if ! grep -qw vfs_read "$TRACE_DIR/available_filter_functions"; then
        echo "vfs_read is not in available_filter_functions" >&2
        return 2
    fi

    reset_trace
    tw current_tracer function_graph || return 1
    tw set_graph_function vfs_read || return 1
    tw max_graph_depth 4 || return 1
    tw options/funcgraph-duration 1 || true
    tw tracing_on 1 || return 1
    run_workload file || return 1
    tw tracing_on 0

    if trace_has vfs_read; then
        show_sample "$TRACE_DIR/trace" vfs_read
        return 0
    fi
    return 1
}

mode_event() {
    reset_trace
    tw events/sched/sched_switch/enable 1 || return 1
    tw events/sched/sched_wakeup/enable 1 || return 1
    tw events/sched/sched_switch/filter 'prev_comm == "trace_workload"' || true
    tw events/sched/sched_wakeup/filter 'comm == "trace_workload"' || true
    tw tracing_on 1 || return 1
    run_workload sched || return 1
    tw tracing_on 0
    tw events/sched/sched_switch/enable 0
    tw events/sched/sched_wakeup/enable 0

    if trace_has sched_switch; then
        show_sample "$TRACE_DIR/trace" sched_switch
        return 0
    fi
    return 1
}

mode_syscall() {
    local pid

    if [[ ! -e "$TRACE_DIR/events/syscalls/sys_enter_getpid/enable" ]]; then
        echo "syscalls:sys_enter_getpid is not available" >&2
        return 2
    fi

    reset_trace
    "$WORKLOAD" syscalls >/dev/null 2>&1 &
    pid=$!

    tw events/syscalls/sys_enter_getpid/filter "common_pid == $pid" || true
    if ! tw events/syscalls/sys_enter_getpid/enable 1; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        return 1
    fi
    tw tracing_on 1
    wait "$pid" 2>/dev/null || true
    tw tracing_on 0
    tw events/syscalls/sys_enter_getpid/enable 0
    tw events/syscalls/sys_enter_getpid/filter 0 || true

    if trace_has sys_getpid || trace_has sys_enter_getpid; then
        show_sample "$TRACE_DIR/trace" sys_getpid
        return 0
    fi
    return 1
}

mode_kprobe() {
    reset_trace
    tw kprobe_events '-:demo_vfs_read' || true
    tw kprobe_events '-:demo_vfs_read_ret' || true

    if ! append kprobe_events 'p:demo_vfs_read vfs_read'; then
        echo "cannot create kprobe on vfs_read" >&2
        return 2
    fi
    if ! append kprobe_events 'r:demo_vfs_read_ret vfs_read ret=$retval'; then
        echo "cannot create kretprobe on vfs_read" >&2
        tw kprobe_events '-:demo_vfs_read' || true
        return 2
    fi

    if [[ ! -e "$TRACE_DIR/events/kprobes/demo_vfs_read/enable" ]]; then
        echo "kprobe event directory was not created" >&2
        tw kprobe_events '-:demo_vfs_read' || true
        tw kprobe_events '-:demo_vfs_read_ret' || true
        return 2
    fi

    tw events/kprobes/demo_vfs_read/enable 1 || return 1
    tw events/kprobes/demo_vfs_read_ret/enable 1 || true
    tw tracing_on 1 || return 1
    run_workload file || return 1
    tw tracing_on 0
    tw events/kprobes/demo_vfs_read/enable 0
    tw events/kprobes/demo_vfs_read_ret/enable 0

    if trace_has demo_vfs_read && trace_has demo_vfs_read_ret; then
        show_sample "$TRACE_DIR/trace" demo_vfs_read
        tw kprobe_events '-:demo_vfs_read' || true
        tw kprobe_events '-:demo_vfs_read_ret' || true
        return 0
    fi

    tw kprobe_events '-:demo_vfs_read' || true
    tw kprobe_events '-:demo_vfs_read_ret' || true
    return 1
}

mode_uprobe() {
    local offset

    if [[ ! -e "$TRACE_DIR/uprobe_events" ]]; then
        echo "uprobe_events is not available" >&2
        return 2
    fi

    offset="$("$WORKLOAD" offset | sed -n 's/^TRACE_TARGET_OFFSET=//p')"
    if [[ -z "$offset" ]]; then
        echo "cannot resolve trace_target offset" >&2
        return 2
    fi

    reset_trace
    tw uprobe_events '-:demo_uprobe' || true
    tw uprobe_events '-:demo_uprobe_ret' || true
    if ! append uprobe_events "p:demo_uprobe $WORKLOAD:$offset"; then
        echo "cannot create uprobe" >&2
        return 2
    fi
    if ! append uprobe_events "r:demo_uprobe_ret $WORKLOAD:$offset"; then
        echo "cannot create uretprobe" >&2
        tw uprobe_events '-:demo_uprobe' || true
        return 2
    fi

    tw events/uprobes/demo_uprobe/enable 1 || return 1
    tw events/uprobes/demo_uprobe_ret/enable 1 || true
    tw tracing_on 1 || return 1
    run_workload target || return 1
    tw tracing_on 0
    tw events/uprobes/demo_uprobe/enable 0
    tw events/uprobes/demo_uprobe_ret/enable 0

    if trace_has demo_uprobe && trace_has demo_uprobe_ret; then
        show_sample "$TRACE_DIR/trace" demo_uprobe
        tw uprobe_events '-:demo_uprobe' || true
        tw uprobe_events '-:demo_uprobe_ret' || true
        return 0
    fi

    tw uprobe_events '-:demo_uprobe' || true
    tw uprobe_events '-:demo_uprobe_ret' || true
    return 1
}

mode_hist() {
    local trigger="$TRACE_DIR/events/sched/sched_wakeup/trigger"

    if [[ ! -e "$trigger" ]]; then
        echo "hist triggers are not available" >&2
        return 2
    fi

    reset_trace
    if ! append events/sched/sched_wakeup/trigger \
        'hist:keys=comm:vals=hitcount:sort=hitcount:size=128'; then
        echo "cannot create hist trigger" >&2
        return 2
    fi
    tw events/sched/sched_wakeup/enable 1 || return 1
    tw tracing_on 1 || return 1
    run_workload sched || return 1
    tw tracing_on 0
    tw events/sched/sched_wakeup/enable 0

    if grep -q trace_workload "$TRACE_DIR/events/sched/sched_wakeup/hist" 2>/dev/null; then
        echo "--- sched_wakeup histogram ---"
        cat "$TRACE_DIR/events/sched/sched_wakeup/hist"
        append events/sched/sched_wakeup/trigger \
            '!hist:keys=comm:vals=hitcount' || true
        return 0
    fi

    append events/sched/sched_wakeup/trigger \
        '!hist:keys=comm:vals=hitcount' || true
    return 1
}

mode_synthetic() {
    local print_trigger="$TRACE_DIR/events/ftrace/print/trigger"

    if [[ ! -e "$TRACE_DIR/synthetic_events" || ! -e "$print_trigger" ||
        ! -e "$TRACE_DIR/trace_marker" ]]; then
        echo "synthetic events, ftrace:print, or trace_marker is not available" >&2
        return 2
    fi

    reset_trace
    clear_synthetic
    if ! append synthetic_events 'demo_latency u64 lat'; then
        echo "cannot define synthetic event" >&2
        clear_synthetic
        return 2
    fi

    if ! append events/ftrace/print/trigger \
        'hist:keys=common_pid:ts0=common_timestamp.usecs if buf == "start"'; then
        echo "cannot create start histogram" >&2
        clear_synthetic
        return 2
    fi
    if ! append events/ftrace/print/trigger \
        'hist:keys=common_pid:lat=common_timestamp.usecs-$ts0:onmatch(ftrace.print).trace(demo_latency,$lat) if buf == "end"'; then
        echo "cannot create end histogram" >&2
        clear_synthetic
        return 2
    fi
    for _ in $(seq 1 50); do
        [[ -e "$TRACE_DIR/events/synthetic/demo_latency/trigger" ]] && break
        sleep 0.01
    done
    if [[ ! -e "$TRACE_DIR/events/synthetic/demo_latency/trigger" ]]; then
        echo "synthetic event instance was not created" >&2
        clear_synthetic
        return 2
    fi
    if ! append events/synthetic/demo_latency/trigger 'hist:keys=lat,common_pid:sort=lat'; then
        echo "cannot create synthetic histogram" >&2
        clear_synthetic
        return 2
    fi

    tw events/synthetic/demo_latency/enable 1 || true
    tw tracing_on 1 || return 1
    if ! "$WORKLOAD" marker "$TRACE_DIR/trace_marker" >/dev/null 2>&1; then
        tw tracing_on 0
        clear_synthetic
        return 1
    fi
    tw tracing_on 0
    tw events/synthetic/demo_latency/enable 0 || true

    if grep -q 'hitcount:' "$TRACE_DIR/events/synthetic/demo_latency/hist" 2>/dev/null; then
        echo "--- synthetic demo_latency histogram ---"
        cat "$TRACE_DIR/events/synthetic/demo_latency/hist" 2>/dev/null || true
        clear_synthetic
        return 0
    fi
    clear_synthetic
    return 1
}

mode_snapshot() {
    if [[ ! -e "$TRACE_DIR/snapshot" ]]; then
        echo "snapshot support is not available" >&2
        return 2
    fi
    if ! grep -qw function_graph "$TRACE_DIR/available_tracers"; then
        echo "function_graph tracer is not available" >&2
        return 2
    fi
    if ! grep -qw vfs_read "$TRACE_DIR/available_filter_functions"; then
        echo "vfs_read is not traceable" >&2
        return 2
    fi

    reset_trace
    tw current_tracer function_graph || return 1
    tw set_graph_function vfs_read || return 1
    tw tracing_on 1 || return 1
    run_workload file || return 1
    if ! tw snapshot 1; then
        echo "cannot take snapshot" >&2
        tw tracing_on 0
        return 1
    fi
    tw tracing_on 0

    if grep -q vfs_read "$TRACE_DIR/snapshot" 2>/dev/null; then
        show_sample "$TRACE_DIR/snapshot" vfs_read
        return 0
    fi
    return 1
}

mode_instance() {
    local instance="$TRACE_DIR/instances/demo"

    rmdir "$instance" 2>/dev/null || true
    if ! mkdir "$instance" 2>/dev/null; then
        echo "cannot create trace instance" >&2
        return 2
    fi
    if [[ ! -e "$instance/events/sched/sched_switch/enable" ]]; then
        echo "sched_switch is not available in the instance" >&2
        rmdir "$instance" 2>/dev/null || true
        return 2
    fi

    echo 0 > "$instance/tracing_on" 2>/dev/null || true
    echo 1 > "$instance/events/sched/sched_switch/enable" 2>/dev/null || {
        rmdir "$instance" 2>/dev/null || true
        return 1
    }
    echo 1 > "$instance/tracing_on" 2>/dev/null || {
        rmdir "$instance" 2>/dev/null || true
        return 1
    }
    run_workload sched || {
        echo 0 > "$instance/tracing_on" 2>/dev/null || true
        rmdir "$instance" 2>/dev/null || true
        return 1
    }
    echo 0 > "$instance/tracing_on" 2>/dev/null || true
    echo 0 > "$instance/events/sched/sched_switch/enable" 2>/dev/null || true

    if grep -q sched_switch "$instance/trace" 2>/dev/null; then
        show_sample "$instance/trace" sched_switch
        rmdir "$instance" 2>/dev/null || true
        return 0
    fi

    rmdir "$instance" 2>/dev/null || true
    return 1
}

mode_pipe() {
    local output="$TMP_DIR/trace_pipe.out"
    local pid

    if [[ ! -e "$TRACE_DIR/trace_marker" ]]; then
        echo "trace_marker is not available" >&2
        return 2
    fi

    reset_trace
    : > "$output"
    cat "$TRACE_DIR/trace_pipe" > "$output" 2>/dev/null &
    pid=$!
    PIPE_PID=$pid

    tw tracing_on 1 || true
    "$WORKLOAD" marker "$TRACE_DIR/trace_marker" >/dev/null 2>&1 || true
    sleep 0.2
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    PIPE_PID=""
    tw tracing_on 0 || true

    if grep -q start "$output" 2>/dev/null; then
        echo "--- trace_pipe sample ---"
        head -20 "$output"
        return 0
    fi
    return 1
}

mode_stacktrace() {
    local event="sched/sched_switch"
    local trigger="$TRACE_DIR/events/$event/trigger"

    if [[ ! -e "$TRACE_DIR/events/kmem/kmalloc/trigger" ]]; then
        echo "kmem/kmalloc is not available; using sched_switch" >&2
    fi
    if [[ -e "$TRACE_DIR/events/kmem/kmalloc/trigger" ]]; then
        event="kmem/kmalloc"
        trigger="$TRACE_DIR/events/$event/trigger"
    fi

    if [[ ! -e "$trigger" ]]; then
        echo "stacktrace trigger is not available" >&2
        return 2
    fi

    reset_trace
    if ! append "events/$event/trigger" 'stacktrace'; then
        echo "cannot create stacktrace trigger" >&2
        return 2
    fi
    tw "events/$event/enable" 1 || return 1
    tw tracing_on 1 || return 1
    run_workload sched || return 1
    tw tracing_on 0
    tw "events/$event/enable" 0

    if grep -q '=>' "$TRACE_DIR/trace" 2>/dev/null; then
        echo "--- stacktrace sample ---"
        grep -m 3 -A 8 -- '=>' "$TRACE_DIR/trace" | head -30
        append "events/$event/trigger" '!stacktrace' || true
        return 0
    fi

    append "events/$event/trigger" '!stacktrace' || true
    return 1
}

run_mode() {
    local name="$1"
    local rc

    echo
    echo "=== $name ==="
    "mode_$name"
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
    fail "this script must be run as root (tracefs access)"
fi

ensure_tracefs || fail "tracefs is not available; rebuild the kernel with tracing enabled"

make -C "$SCRIPT_DIR" >/dev/null || fail "failed to build trace_workload"

case "${1:-all}" in
    all)
        for mode in "${MODES[@]}"; do
            run_mode "$mode"
        done
        ;;
    function|event|syscall|kprobe|uprobe|hist|synthetic|snapshot|instance|pipe|stacktrace)
        run_mode "$1"
        ;;
    *)
        echo "Usage: $0 [all|function|event|syscall|kprobe|uprobe|hist|synthetic|snapshot|instance|pipe|stacktrace]"
        exit 1
        ;;
esac

echo
echo "Passed: $PASS  Skipped: $SKIP  Failed: $FAIL"
[[ "$FAIL" -eq 0 ]]
