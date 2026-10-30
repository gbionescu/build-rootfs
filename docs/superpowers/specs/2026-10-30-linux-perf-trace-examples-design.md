# Comprehensive Linux perf and trace examples

Date: 2026-10-30
Status: Approved design

## Goal

Expand `linux-app/kernel-perf` and `linux-app/kernel-trace` into self-contained,
self-checking examples that cover the main userspace-visible aspects of Linux perf
and ftrace, without depending on the `perf`, `trace-cmd`, or `bpftrace` commands.

## Non-goals

- No eBPF or bpftrace examples.
- No `perf record`/`perf report` CLI tutorial.
- No rebuild or replacement of the tracked pre-built kernel images.
- No new guest packages; the examples use libc, `perf_event_open(2)`, and tracefs.
- No change to `run-all.sh`; perf and trace need root and kernel tracing features.

## Decisions

- Keep the existing directory names `kernel-perf` and `kernel-trace`; replace their
  contents with the comprehensive versions.
- Use one multi-mode C program for perf and one shell driver plus workload for trace.
- Add a header-only ELF symbol helper shared by both examples so uprobes do not need
  `nm`.
- Enable missing kernel options in the kernel build path. Users must rebuild the kernel
  to use the new features; old kernels cause affected modes to report `SKIP`.
- Exit code contract for individual modes: `0` pass, `1` fail, `2` unsupported/skipped.

## Layout

```text
linux-app/
├── common/
│   └── elf_symbol.h
├── kernel-perf/
│   ├── perf_demo.c
│   ├── run_perf.sh
│   ├── Makefile
│   └── README.md
├── kernel-trace/
│   ├── trace_workload.c
│   ├── trace_demo.sh
│   ├── Makefile
│   └── README.md
```

`linux-app/Makefile` gains `test-perf` and `test-trace` convenience targets.
`linux-app/README.md` and the top-level `README.md` are updated.

## Common ELF helper

`common/elf_symbol.h` provides:

```c
static int elf_find_symbol_offset(const char *path, const char *name,
                                  unsigned long long *offset);
```

- Supports ELF32 and ELF64.
- Reads section headers, finds `SHT_SYMTAB` or `SHT_DYNSYM`, and matches the symbol
  name in the linked string table.
- Converts `st_value` to a file offset using `PT_LOAD` program headers, so PIE and
  non-PIE executables both work.
- Returns `0` on success and `-1` when the file, symbol table, symbol, or load mapping
  is missing.

## perf modes

`perf_demo <mode>`:

| Mode | Behavior |
|------|----------|
| `stat` | Counts software and hardware events for the current task, reports IPC and unsupported events. |
| `group` | Opens a group leader plus members with `PERF_FORMAT_GROUP`, reads the group in one call, and shows `time_enabled`/`time_running` scaling. |
| `sample` | Samples `PERF_COUNT_SW_CPU_CLOCK` through an mmap ring buffer; parses `PERF_RECORD_SAMPLE` with IP/TID/TIME/CPU/CALLCHAIN; reports samples and lost records. |
| `tracepoint` | Reads a tracepoint id from tracefs and counts `raw_syscalls:sys_enter` through `PERF_TYPE_TRACEPOINT`. |
| `kprobe` | Creates `p:perf_demo_vfs_read vfs_read` through tracefs, opens its perf tracepoint id, counts file I/O, and removes the probe. |
| `uprobe` | Finds the offset of `uprobe_target()` in `/proc/self/exe`, creates a uprobe, and counts calls to the function. |
| `pid` | Forks a busy child, attaches an event to the child PID, and counts only that child. |
| `cpu` | Opens a system-wide event with `pid=-1, cpu=0`, pins the workload to CPU 0, and counts all activity. |

`run_perf.sh [all|mode]` checks for root, makes tracefs available, builds the program,
runs modes, and prints a PASS/SKIP/FAIL summary.

## trace modes

`trace_demo.sh [all|mode]`:

| Mode | Behavior |
|------|----------|
| `function` | Uses `function_graph`, `set_graph_function` on `vfs_read`, and `max_graph_depth`; runs file I/O. |
| `event` | Enables `sched_switch`/`sched_wakeup`, applies a `prev_comm` filter, and runs a scheduling workload. |
| `syscall` | Runs `syscalls:sys_enter_getpid` with a `common_pid` filter for a background workload PID. |
| `kprobe` | Creates entry and return probes on `vfs_read`; the return probe reads `$retval`. |
| `uprobe` | Uses the ELF offset of `trace_target()` to create entry and return uprobes on the workload. |
| `hist` | Adds `hist:keys=comm:vals=hitcount:sort=hitcount:size=128` to `sched_wakeup`, runs a scheduling workload, and prints the histogram. |
| `synthetic` | Writes `start`/`end` markers to `trace_marker`, builds a latency histogram and a synthetic `demo_latency` event, and verifies the aggregated output. |
| `snapshot` | Traces `vfs_read`, runs file I/O, then freezes the buffer with `echo 1 > snapshot` and reads the snapshot. |
| `instance` | Creates `instances/demo`, enables scheduling tracepoints inside it, and reads its private trace buffer. |
| `pipe` | Streams `trace_pipe` while the workload writes markers, then verifies the live output. |
| `stacktrace` | Adds a `stacktrace` trigger to `kmem:kmalloc`, runs an allocating workload, and verifies call-stack output. |

`trace_workload.c` provides workload modes: `file`, `syscalls`, `sched`, `target`,
`marker`, and `offset`. The `sched` workload forks children to force context switches.
The `marker` workload writes to a tracefs `trace_marker` path supplied by the script.
The `offset` mode prints the file offset of `trace_target()` for uprobe setup.

Every mode cleans up the tracefs files it changes. Missing tracers, events, triggers, or
config options cause `SKIP`, not `FAIL`.

## Kernel configuration

Add the following options to `TRACE_CONFIG` in `kernel/build-kernel.sh`:

```text
UPROBES UPROBE_EVENTS DYNAMIC_EVENTS
HIST_TRIGGERS SYNTH_EVENTS TRACER_SNAPSHOT STACKTRACE
```

Mirror the changes in `kernel/.config`, `kernel/.config-arm64`, `.config-arm32`, and
`.config-riscv64`. Update `kernel/README.md` with the new feature list and note that the
tracked kernel images must be rebuilt to use the new trace modes.

## Compatibility and error handling

- Locate tracefs at `/sys/kernel/tracing`, then `/sys/kernel/debug/tracing`.
- Mount tracefs in the runner scripts when running as root if it is not mounted.
- Hardware PMU events may be unsupported under QEMU TCG; `stat` continues with software
  events and `group` drops unsupported members.
- `run_perf.sh` requires root or a low `kernel.perf_event_paranoid`.
- `trace_demo.sh` requires root and a kernel with tracing enabled.
- Use `trap cleanup EXIT` in shell scripts and remove dynamic probes in all exit paths.
- Do not write partial probe definitions when a tracefs file is read-only or missing.

## Testing

1. `make -C linux-app` builds all existing and new examples on the host.
2. Cross-compile both examples for arm64, arm32, and riscv64.
3. `bash -n` validates both shell scripts; run `shellcheck` when available.
4. A small unit check compares `elf_find_symbol_offset()` against `nm` for a PIE and a
   non-PIE binary.
5. In a QEMU guest as root, run `./kernel-perf/run_perf.sh all` and
   `./kernel-trace/trace_demo.sh all`. Modes requiring unbuilt kernel options must
   report `SKIP`.
6. Existing `run-all.sh` tests remain unchanged and pass.

## Acceptance criteria

1. `kernel-perf` demonstrates counting, grouping, sampling, tracepoints, kprobes,
   uprobes, per-PID, and per-CPU perf use.
2. `kernel-trace` demonstrates function graph, tracepoints, syscall filters, kprobes,
   uprobes, hist triggers, synthetic events, snapshots, instances, `trace_pipe`, and
   stack traces.
3. Both runners print a pass/skip/fail summary and exit non-zero on failure.
4. No new runtime package is required.
5. Building on the old kernel still succeeds, and unsupported modes skip cleanly.
6. Kernel configs and build scripts enable the new trace features for all architectures.

## Validation already performed

On 2026-10-30:

- Host `make -C linux-app` and `-Werror` builds passed.
- The examples cross-compiled for arm64, arm32, and riscv64.
- `tests/test-elf-symbol.sh` passed for PIE and non-PIE inputs.
- In an arm64 QEMU guest with the tracked 6.1.22 kernel, `run_perf.sh all`
  passed 8/8 and `trace_demo.sh all` passed 8, skipped 3 (hist, synthetic,
  snapshot), and failed 0.
- With a temporary arm64 kernel rebuilt from the updated config, `trace_demo.sh
  all` passed 11/11 and `run_perf.sh all` passed 8/8.
- After `trace_demo.sh all`, sched/print/synthetic triggers, synthetic events,
  instances, kprobes, and uprobes were all removed and `tracing_on`/`current_tracer`
  were restored.
