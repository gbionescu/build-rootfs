# Linux perf and trace examples Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `linux-app/kernel-perf` and `linux-app/kernel-trace` with comprehensive, self-checking perf_event_open and tracefs examples.

**Architecture:** One multi-mode C program for perf, one shell driver plus C workload for trace, and a shared header-only ELF symbol helper for uprobes. Modes return pass/fail/skip and runners aggregate results. Kernel configs gain the trace features required by the advanced modes.

**Tech Stack:** C11/GNU C, `perf_event_open(2)`, `mmap` ring buffers, tracefs, bash, GNU make, QEMU guest.

## Global Constraints

- No `perf`, `trace-cmd`, `bpftrace`, or eBPF dependencies.
- No new guest packages; libc, pthread, and tracefs only.
- Keep directory names `linux-app/kernel-perf` and `linux-app/kernel-trace`.
- Mode exit codes: `0` pass, `1` fail, `2` unsupported/skipped.
- Do not commit changes or rebuild tracked kernel images.
- Tracefs lookup order: `/sys/kernel/tracing`, then `/sys/kernel/debug/tracing`.
- All shell scripts use `set -u` and `trap cleanup EXIT`.

---

### Task 1: Shared ELF symbol helper

**Files:**
- Create: `linux-app/common/elf_symbol.h`
- Create: `linux-app/common/elf_symbol_test.c`
- Create: `tests/test-elf-symbol.sh`

**Interfaces:**
- Produces: `static int elf_find_symbol_offset(const char *path, const char *name, unsigned long long *offset);` returns `0` on success, `-1` otherwise.
- Produces: test binary that prints `PASS`/`FAIL` for PIE and non-PIE inputs.

- [ ] Write `elf_symbol.h` using `Elf32_*`/`Elf64_*` selected by `__SIZEOF_POINTER__`, parse `SHT_SYMTAB`/`SHT_DYNSYM`, and map `st_value` through `PT_LOAD`.
- [ ] Write `elf_symbol_test.c` that accepts `<binary> <symbol> <expected-offset>` and prints `PASS` or `FAIL`.
- [ ] Write `tests/test-elf-symbol.sh` that builds PIE and `-no-pie` targets with a `noinline` symbol, obtains the reference offset with `nm`, and runs the helper.
- [ ] Run: `./tests/test-elf-symbol.sh`; expected: both cases print `PASS`.

### Task 2: perf core modes

**Files:**
- Create: `linux-app/kernel-perf/perf_demo.c`
- Create: `linux-app/kernel-perf/Makefile`
- Modify: `linux-app/.gitignore`

**Interfaces:**
- Produces: `perf_demo stat|group|sample` with exit codes `0/1/2`.
- Produces shared helpers: `perf_event_open()`, `monotonic_ns()`, `find_tracefs()`, `read_tracefs_id()`, workload functions.

- [ ] Implement `stat` by porting the existing counter table and adding duration parsing.
- [ ] Implement `group` with `PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING`, parse `{nr, time_enabled, time_running, values[]}`, and print scaling.
- [ ] Implement `sample` with an 8-page mmap ring buffer, `PERF_SAMPLE_IP|TID|TIME|CPU|CALLCHAIN`, `PERF_RECORD_SAMPLE` and `PERF_RECORD_LOST` parsing, and the first five samples printed.
- [ ] Add `Makefile` target `perf_demo` with `-D_GNU_SOURCE -Wall -Wextra -O2 -g`.
- [ ] Run: `make -C linux-app/kernel-perf`; expected: clean build.
- [ ] Run: `./linux-app/kernel-perf/perf_demo stat 200`; expected: usage/error path works without root; with root it prints counters.

### Task 3: perf advanced modes and runner

**Files:**
- Modify: `linux-app/kernel-perf/perf_demo.c`
- Create: `linux-app/kernel-perf/run_perf.sh`

**Interfaces:**
- Consumes: `elf_find_symbol_offset()` from Task 1.
- Produces: `perf_demo tracepoint|kprobe|uprobe|pid|cpu`.
- Produces: `run_perf.sh [all|mode]` summary with PASS/SKIP/FAIL.

- [ ] Implement `tracepoint` by reading `events/raw_syscalls/sys_enter/id` and opening `PERF_TYPE_TRACEPOINT`.
- [ ] Implement `kprobe` by writing `p:perf_demo_vfs_read vfs_read` to `kprobe_events`, opening its id, running file I/O, and removing the probe.
- [ ] Implement `uprobe` by resolving `uprobe_target` in `/proc/self/exe`, creating `p:perf_demo_target <path>:0x<offset>`, and counting calls.
- [ ] Implement `pid` with a pipe-synchronized forked child and a counter attached to the child PID.
- [ ] Implement `cpu` with `pid=-1, cpu=0`, CPU affinity, and a system-wide software event.
- [ ] Implement `run_perf.sh` with root check, tracefs mount, `make -s`, mode loop, `2 -> SKIP`, and non-zero exit on failure.
- [ ] Run: `bash -n linux-app/kernel-perf/run_perf.sh`; expected: no output.

### Task 4: trace workload and runner skeleton

**Files:**
- Create: `linux-app/kernel-trace/trace_workload.c`
- Create: `linux-app/kernel-trace/Makefile`
- Create: `linux-app/kernel-trace/trace_demo.sh`

**Interfaces:**
- Produces workload modes: `file`, `syscalls`, `sched`, `target`, `marker <path>`, `offset`.
- Produces `trace_demo.sh [all|mode]` with shared `find_tracefs`, `reset_trace`, `cleanup`, `run_mode`.

- [ ] Implement `file` workload (64 KiB write/read rounds through `/tmp`).
- [ ] Implement `syscalls` workload with a startup delay, `getpid()` loop, and `openat()` loop.
- [ ] Implement `sched` workload with forked children, `sched_yield`, and `usleep`.
- [ ] Implement `target` with `__attribute__((noinline)) void trace_target(int)` and a volatile sink.
- [ ] Implement `marker` writing `start`/`end` to the supplied `trace_marker` path.
- [ ] Implement `offset` printing `TRACE_TARGET_OFFSET=0x...` via `elf_symbol.h`.
- [ ] Write the runner skeleton with root/tracefs checks, `reset_trace`, cleanup of sched events and dynamic probes, and a mode dispatch table.
- [ ] Run: `make -C linux-app/kernel-trace` and `./linux-app/kernel-trace/trace_workload offset`; expected: clean build and a `TRACE_TARGET_OFFSET=` line.

### Task 5: trace basic modes

**Files:**
- Modify: `linux-app/kernel-trace/trace_demo.sh`

**Interfaces:**
- Consumes: workload modes from Task 4.
- Produces modes: `function`, `event`, `syscall`, `kprobe`, `uprobe`.

- [ ] Implement `function` with `function_graph`, `set_graph_function vfs_read`, `max_graph_depth=4`, and a `vfs_read` trace check.
- [ ] Implement `event` with `sched_switch`/`sched_wakeup`, a `prev_comm == "trace_workload"` filter, and a trace check.
- [ ] Implement `syscall` by starting `trace_workload syscalls` in the background, applying `common_pid == $pid`, enabling `syscalls:sys_enter_getpid`, and checking the trace.
- [ ] Implement `kprobe` with `p:demo_vfs_read vfs_read` and `r:demo_vfs_read_ret vfs_read ret=$retval`; verify both event names and remove both.
- [ ] Implement `uprobe` using `trace_workload offset`, entry/return uprobes, `trace_workload target`, and cleanup.
- [ ] Run each mode as root in a guest; expected: `[PASS]` or `SKIP` when unsupported.

### Task 6: trace advanced modes

**Files:**
- Modify: `linux-app/kernel-trace/trace_demo.sh`

**Interfaces:**
- Produces modes: `hist`, `synthetic`, `snapshot`, `instance`, `pipe`, `stacktrace`.

- [ ] Implement `hist` with `hist:keys=comm:vals=hitcount:sort=hitcount:size=128` on `sched_wakeup`; verify `trace_workload` appears and clear the trigger.
- [ ] Implement `synthetic` with `demo_latency u64 lat`, `ftrace:print` histograms for `start`/`end`, an `onmatch(ftrace.print).trace(demo_latency,$lat)` action, and the synthetic histogram check.
- [ ] Implement `snapshot` with `function_graph` on `vfs_read`, `echo 1 > snapshot`, and a snapshot content check.
- [ ] Implement `instance` with `instances/demo`, sched tracepoints inside the instance, and a private trace check.
- [ ] Implement `pipe` by streaming `trace_pipe` while `trace_workload marker` runs, then checking the captured output.
- [ ] Implement `stacktrace` with a `stacktrace` trigger on `kmem:kmalloc`; verify `=>` stack frames and clear the trigger.
- [ ] Run: `bash -n linux-app/kernel-trace/trace_demo.sh`; expected: no output.

### Task 7: kernel configs, build wiring, and docs

**Files:**
- Modify: `kernel/build-kernel.sh`
- Modify: `kernel/.config`, `kernel/.config-arm64`, `kernel/.config-arm32`, `kernel/.config-riscv64`
- Modify: `kernel/README.md`
- Modify: `linux-app/Makefile`
- Modify: `linux-app/README.md`
- Modify: `README.md`
- Modify: `linux-app/.gitignore`
- Modify: `linux-app/kernel-perf/README.md`
- Modify: `linux-app/kernel-trace/README.md`

**Interfaces:**
- Produces `make test-perf` and `make test-trace` targets.
- Produces documented prerequisites, modes, and kernel rebuild instructions.

- [ ] Add `UPROBES UPROBE_EVENTS DYNAMIC_EVENTS HIST_TRIGGERS SYNTH_EVENTS TRACER_SNAPSHOT STACKTRACE` to `TRACE_CONFIG`.
- [ ] Flip the corresponding `# CONFIG_... is not set` lines to `=y` in all four config files.
- [ ] Add `test-perf`/`test-trace` targets to `linux-app/Makefile`.
- [ ] Update `.gitignore` for `kernel-perf/perf_demo` and `kernel-trace/trace_workload`.
- [ ] Rewrite the two example READMEs with mode tables, prerequisites, build/run commands, and SKIP semantics.
- [ ] Update `linux-app/README.md`, root `README.md`, and `kernel/README.md`.
- [ ] Run: `make -C linux-app`; expected: all existing and new host examples build.
- [ ] Cross-compile: `make -C linux-app/kernel-perf CC=aarch64-linux-gnu-gcc`, then arm32 and riscv64; repeat for `kernel-trace`.
- [ ] Run: `./tests/test-elf-symbol.sh`; expected: PASS.
- [ ] Run: `bash -n` on both runners; expected: no output.
- [ ] If a guest is available, boot it and run both `all` modes as root; expected: no FAIL, unsupported modes SKIP.

## Self-Review

- Spec coverage: all perf and trace modes in the spec map to Tasks 2-6; config/docs/testing map to Task 7.
- Placeholder scan: no TBD/TODO; each task has concrete files, interfaces, and commands.
- Type consistency: `elf_find_symbol_offset()` signature is identical in Tasks 1, 3, and 4; workload mode names and runner modes match the spec.
