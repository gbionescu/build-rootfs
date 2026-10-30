# Linux eBPF examples for linux-app

Date: 2026-10-30
Status: Approved design

## Goal

Add a self-contained eBPF example suite under `linux-app/ebpf` that demonstrates
map operations, socket filters, tracing programs, performance event output, and
the verifier, using only the raw `bpf(2)` syscall and hand-written eBPF
instructions.

## Non-goals

- No clang, libbpf, BCC, bpftrace, or BTF dependency.
- No eBPF CO-RE, skeleton, or global-variable relocation.
- No XDP, TC, cgroup, LSM, or networking beyond a UDP socket filter.
- No kernel config or pre-built kernel image changes; the existing kernel already
  has the required BPF options.

## Decisions

- Implement one multi-mode C program, `ebpf_demo`, plus a self-checking shell
  runner, consistent with `kernel-perf` and `kernel-trace`.
- Hand-write eBPF instruction arrays with small emitter macros; do not embed an
  ELF object.
- Use `perf_event_open` plus `PERF_EVENT_IOC_SET_BPF` for kprobe, tracepoint,
  and uprobe attachment.
- Reuse `linux-app/common/elf_symbol.h` for uprobe symbol offsets.
- Exit codes: `0` pass, `1` fail, `2` unsupported/skipped.

## Layout

```text
linux-app/ebpf/
├── bpf_insn.h
├── bpf_sys.h
├── ebpf_demo.c
├── run_ebpf.sh
├── Makefile
└── README.md
```

`linux-app/Makefile` gains `ebpf_demo` in `SUBDIRS` and a `test-ebpf` target.
`linux-app/README.md` and the top-level `README.md` are updated. `.gitignore`
ignores `ebpf/ebpf_demo`.

## Instruction and syscall helpers

`bpf_insn.h` defines:

- `BPF_INSN()` plus MOV, ALU, LDX, STX, ST, JMP, CALL, EXIT macros.
- `emit_ld_imm64()` for 64-bit immediates.
- `emit_ld_map_fd()` for `BPF_PSEUDO_MAP_FD` map references.
- `BPF_STX_XADD()` for atomic map value increments.

`bpf_sys.h` provides static wrappers:

- `bpf_sys()`, `bpf_map_create()`, `bpf_map_update()`, `bpf_map_lookup()`,
  `bpf_map_delete()`, `bpf_map_get_next_key()`.
- `bpf_prog_load()` with a verifier log buffer.
- `bpf_obj_get_info_by_fd()`.
- `find_tracefs()`, `tracefs_event_id()`, `write_tracefs()`.
- `perf_event_open()`, `tracepoint_perf_fd()`, `attach_bpf_to_perf()`.

## Modes

| Mode | Behavior |
|------|----------|
| `map` | Creates a hash map and an array map; demonstrates update, lookup, iteration, delete, and out-of-range array access. |
| `socket` | Loads socket-filter programs returning `0xffff` and `0`; attaches with `SO_ATTACH_BPF`; verifies UDP receive and drop; prints program id, tag, and JIT length. |
| `counter` | Loads a socket filter that looks up an array map and atomically increments value 0; sends ten UDP datagrams and verifies the counter. |
| `kprobe` | Creates a kprobe on `vfs_read`, attaches a KPROBE counter program through perf, runs file I/O, and verifies the map count. |
| `tracepoint` | Attaches a TRACEPOINT counter program to `sched:sched_switch`, runs a scheduling workload, and verifies the map count. |
| `uprobe` | Resolves `ebpf_target()` in `/proc/self/exe`, creates a uprobe, attaches a KPROBE program, calls the target, and verifies the map count. |
| `trace-printk` | Attaches a kprobe program that calls `bpf_trace_printk("hit %d\n", pid)`, then reads the trace buffer for the output. |
| `perf-output` | Builds `comm[16] + pid` on the BPF stack, calls `bpf_perf_event_output` through a perf event array, and parses `PERF_SAMPLE_RAW` records on CPU 0. |
| `verifier` | Attempts to load a program reading `[r10+8]`; expects rejection and prints the verifier log. |

`run_ebpf.sh [all|mode]` checks root, ensures tracefs, builds the program, runs
the selected modes, and prints a PASS/SKIP/FAIL summary.

## Workloads

`ebpf_demo.c` includes file, scheduling, and target workloads:

- File I/O creates a temporary file and repeatedly reads/writes 64 KiB.
- Scheduling forks children that yield and sleep.
- The target workload calls a noinline `ebpf_target()` function.
- The perf-output workload pins the process to CPU 0 before file I/O.

## Error handling and cleanup

- Root is required because `BPF_UNPRIV_DEFAULT_OFF=y`.
- Missing tracefs, kprobe/uprobe events, perf BPF attach support, or map/prog
  features return `2` (SKIP).
- Verifier and program load failures print the verifier log.
- Dynamic kprobe/uprobe names are removed on every exit path.
- Perf fds, mmap regions, map fds, and program fds are closed.
- `tracing_on` is restored to its original value after `trace-printk`.

## Kernel requirements

The current kernel already enables:

```text
BPF_SYSCALL BPF_JIT BPF_EVENTS PERF_EVENTS
KPROBE_EVENTS UPROBE_EVENTS TRACEPOINTS TRACING
```

BTF is not required. No kernel configuration or image rebuild is needed.

## Testing

1. `make -C linux-app/ebpf` with `-Wall -Wextra -Werror`.
2. Cross-compile for arm64, arm32, and riscv64.
3. In an arm64 QEMU guest as root, run `./run_ebpf.sh all`; expect nine passes,
   or SKIP for features blocked by the environment.
4. Verify after the run that kprobes/uprobes are gone, `tracing_on` is restored,
   and the process exits cleanly.
5. Existing `make -C linux-app`, `run-all.sh`, and ELF symbol tests still pass.

## Acceptance criteria

1. All nine modes are implemented and documented.
2. No new packages or kernel rebuild are required.
3. The runner prints PASS/SKIP/FAIL and exits non-zero on failure.
4. Map, socket, kprobe, tracepoint, uprobe, trace-printk, perf-output, and
   verifier behavior is demonstrated with actual kernel interaction.
5. Cleanup leaves no dynamic probes or perf events behind.

## Validation already performed

On 2026-10-30:

- Host `-Werror` build passed.
- Cross-compilation for arm64, arm32, and riscv64 passed.
- In an arm64 QEMU guest with the tracked 6.1.22 kernel, `run_ebpf.sh all`
  passed 9/9: map, socket, counter, kprobe, tracepoint, uprobe, trace-printk,
  perf-output, and verifier.
- Observed outputs included a socket-filter counter of 10, a kprobe count of 8,
  1784 `sched_switch` hits, 75391 uprobe hits, `bpf_trace_printk: hit 311`, and
  eight `PERF_SAMPLE_RAW` records.
- After the full run, `kprobe_events` and `uprobe_events` were empty and
  `current_tracer` was `nop`; `tracing_on` was restored to its original value.
- Existing `make -C linux-app`, `run-all.sh`, and the ELF symbol test still pass.
