# Linux eBPF examples Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a raw `bpf(2)` eBPF example suite under `linux-app/ebpf` with nine self-checking modes.

**Architecture:** A multi-mode C program builds hand-written eBPF instruction arrays, loads them through a small syscall wrapper layer, and attaches tracing programs through perf events. A shell runner mounts tracefs, builds the program, and aggregates PASS/SKIP/FAIL.

**Tech Stack:** GNU C, `bpf(2)`, `perf_event_open(2)`, `PERF_EVENT_IOC_SET_BPF`, tracefs, UDP sockets, bash, GNU make.

## Global Constraints

- No clang, libbpf, BCC, bpftrace, or BTF.
- No kernel config or pre-built image changes.
- Root is required.
- Exit codes: `0` pass, `1` fail, `2` skip.
- Dynamic kprobes/uprobes and perf fds must be cleaned up.
- Do not commit changes.

---

### Task 1: Instruction and syscall helpers

**Files:**
- Create: `linux-app/ebpf/bpf_insn.h`
- Create: `linux-app/ebpf/bpf_sys.h`

**Interfaces:**
- Produces: instruction macros `BPF_MOV64_IMM`, `BPF_MOV64_REG`, `BPF_ALU64_IMM`,
  `BPF_ALU64_REG`, `BPF_LDX_MEM`, `BPF_STX_MEM`, `BPF_ST_MEM`, `BPF_STX_XADD`,
  `BPF_JMP_IMM`, `BPF_CALL_FUNC`, `BPF_EXIT_INSN`.
- Produces: `emit_ld_imm64()`, `emit_ld_map_fd()`.
- Produces: `bpf_map_create`, `bpf_map_update`, `bpf_map_lookup`, `bpf_map_delete`,
  `bpf_map_get_next_key`, `bpf_prog_load`, `bpf_obj_get_info_by_fd`.
- Produces: `find_tracefs`, `tracefs_event_id`, `write_tracefs`, `perf_event_open`,
  `tracepoint_perf_fd`, `attach_bpf_to_perf`.

- [ ] Write `bpf_insn.h` with the macro set and 64-bit/map-fd emitters.
- [ ] Write `bpf_sys.h` with `union bpf_attr` wrappers and tracefs helpers.
- [ ] Compile a small include test with `gcc -Wall -Wextra -Werror`.
- [ ] Run: `gcc -D_GNU_SOURCE -Wall -Wextra -Werror -fsyntax-only linux-app/ebpf/ebpf_demo.c` after Task 2.

### Task 2: Map and socket modes

**Files:**
- Create: `linux-app/ebpf/ebpf_demo.c`
- Create: `linux-app/ebpf/Makefile`

**Interfaces:**
- Consumes: helpers from Task 1.
- Produces: `ebpf_demo map|socket|counter` with exit codes `0/1/2`.

- [ ] Implement `map` for hash CRUD/iteration and array bounds.
- [ ] Implement simple socket-filter programs returning `0xffff` and `0`.
- [ ] Attach with `SO_ATTACH_BPF`; verify UDP receive and drop with `poll`.
- [ ] Print `BPF_OBJ_GET_INFO_BY_FD` id/tag/JIT length.
- [ ] Implement `counter`: array map, `map_lookup_elem`, atomic `XADD`, ten UDP packets.
- [ ] Run: `make -C linux-app/ebpf`; expected clean build.

### Task 3: Tracing modes

**Files:**
- Modify: `linux-app/ebpf/ebpf_demo.c`

**Interfaces:**
- Produces: `ebpf_demo kprobe|tracepoint|uprobe|trace-printk`.

- [ ] Add file, scheduling, and noinline target workloads.
- [ ] Implement a reusable counter program and dynamic kprobe/uprobe setup.
- [ ] Implement `kprobe` on `vfs_read` with perf attach.
- [ ] Implement `tracepoint` on `sched:sched_switch` with perf attach.
- [ ] Implement `uprobe` using `elf_find_symbol_offset()` and `ebpf_target`.
- [ ] Implement `trace-printk` with a stack format string and tracefs read.
- [ ] Verify each mode in QEMU; expected PASS or SKIP.

### Task 4: Perf output and verifier modes

**Files:**
- Modify: `linux-app/ebpf/ebpf_demo.c`

**Interfaces:**
- Produces: `ebpf_demo perf-output|verifier`.

- [ ] Create a perf event array map and `PERF_COUNT_SW_BPF_OUTPUT` event on CPU 0.
- [ ] Build `comm[16] + pid` on the BPF stack and call `bpf_perf_event_output`.
- [ ] Pin the workload to CPU 0 and parse `PERF_SAMPLE_RAW` records.
- [ ] Implement `verifier` with an invalid `[r10+8]` load and print the log.
- [ ] Verify `perf-output` receives at least one sample and `verifier` is rejected.

### Task 5: Runner and documentation

**Files:**
- Create: `linux-app/ebpf/run_ebpf.sh`
- Create: `linux-app/ebpf/README.md`
- Modify: `linux-app/Makefile`
- Modify: `linux-app/README.md`
- Modify: `README.md`
- Modify: `linux-app/.gitignore`

**Interfaces:**
- Produces: `run_ebpf.sh [all|mode]` and `make test-ebpf`.

- [ ] Write the runner with root check, tracefs mount, build, mode loop, and summary.
- [ ] Add `ebpf` to `SUBDIRS` and `test-ebpf` to `linux-app/Makefile`.
- [ ] Add `ebpf/ebpf_demo` to `.gitignore`.
- [ ] Rewrite READMEs with mode table, prerequisites, commands, and SKIP semantics.
- [ ] Run: `bash -n linux-app/ebpf/run_ebpf.sh`; expected no output.

### Task 6: Verification

**Files:**
- No new files.

- [ ] Run host build with `-Werror`.
- [ ] Cross-compile for arm64, arm32, and riscv64.
- [ ] Boot arm64 QEMU guest and run `./run_ebpf.sh all`.
- [ ] Verify all modes PASS or SKIP, with no FAIL.
- [ ] Verify `kprobe_events`, `uprobe_events`, and `tracing_on` after the run.
- [ ] Run existing `make -C linux-app`, `run-all.sh`, and `tests/test-elf-symbol.sh`.

## Self-Review

- Spec coverage: all nine modes map to Tasks 2-4; helpers to Task 1; runner/docs to Task 5; verification to Task 6.
- Placeholder scan: no TBD/TODO; each task names concrete modes and commands.
- Type consistency: helper names and mode names match the spec.
