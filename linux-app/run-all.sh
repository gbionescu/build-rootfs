#!/bin/bash
set -u

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
cd "$SCRIPT_DIR"

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--Wall -Wextra -O2 -g}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/linux-app-demos.XXXXXX")"
SERVER_PID=""

PASS=0
FAIL=0

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$BUILD_DIR"
}
trap cleanup EXIT

compile() {
    local out="$1"
    local src="$2"
    shift 2

    echo "==> compile $src"
    "$CC" $CFLAGS -D_GNU_SOURCE "$src" -o "$BUILD_DIR/$out" "$@"
}

run_demo() {
    local name="$1"
    local bin="$2"
    shift 2

    echo
    echo "=== $name ==="
    if "$BUILD_DIR/$bin" "$@"; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name"
        FAIL=$((FAIL + 1))
    fi
}

run_epoll() {
    local port=$((20000 + $$ % 20000))
    local log="$BUILD_DIR/epoll-server.log"
    local rc

    echo
    echo "=== epoll-server ==="
    "$BUILD_DIR/epoll_server" server "$port" >"$log" 2>&1 &
    SERVER_PID=$!

    if "$BUILD_DIR/epoll_server" client "$port"; then
        rc=0
    else
        rc=1
    fi

    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""

    if [[ "$rc" -ne 0 ]]; then
        echo "--- server log ---"
        cat "$log"
        echo "[FAIL] epoll-server"
        FAIL=$((FAIL + 1))
    else
        echo "[PASS] epoll-server"
        PASS=$((PASS + 1))
    fi
}

compile fork_demo process-fork/fork_demo.c || exit 1
compile pthread_sum pthread-sum/pthread_sum.c -pthread || exit 1
compile pipe_ipc pipe-ipc/pipe_ipc.c || exit 1
compile shm_sem shm-sem/shm_sem.c -pthread -lrt || exit 1
compile signal_demo signal-demo/signal_demo.c || exit 1
compile mmap_file mmap-file/mmap_file.c || exit 1
compile epoll_server epoll-server/epoll_server.c || exit 1

run_demo process-fork fork_demo
run_demo pthread-sum pthread_sum
run_demo pipe-ipc pipe_ipc
run_demo shm-sem shm_sem
run_demo signal-demo signal_demo
run_demo mmap-file mmap_file
run_epoll

echo
echo "Passed: $PASS  Failed: $FAIL"
[[ "$FAIL" -eq 0 ]]
