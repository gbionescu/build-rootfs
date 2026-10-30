# ebpf

用裸 `bpf(2)` 系统调用和手写 eBPF 指令演示 Linux eBPF 的主要用法，不依赖
clang、libbpf、BCC、bpftrace 或 BTF。

## 模式

| 模式 | 说明 |
|------|------|
| `map` | hash/array map 的创建、update、lookup、遍历、delete 和越界检查 |
| `socket` | socket filter 接受/丢弃，`SO_ATTACH_BPF`，程序 id/tag/JIT 信息 |
| `counter` | socket filter 内调用 `bpf_map_lookup_elem`，原子递增 array map |
| `kprobe` | `BPF_PROG_TYPE_KPROBE` 挂到 `vfs_read` kprobe，统计调用次数 |
| `tracepoint` | `BPF_PROG_TYPE_TRACEPOINT` 挂到 `sched:sched_switch` |
| `uprobe` | KPROBE 程序挂到自身 `ebpf_target()` uprobe |
| `trace-printk` | 程序调用 `bpf_trace_printk("hit %d\n", pid)`，从 tracefs 读取输出 |
| `perf-output` | `BPF_MAP_TYPE_PERF_EVENT_ARRAY` + `bpf_perf_event_output`，用户态解析 raw 样本 |
| `verifier` | 加载非法 `[r10+8]` 访问程序，打印 verifier 日志 |

## 前置条件

- root。当前内核 `CONFIG_BPF_UNPRIV_DEFAULT_OFF=y`，普通用户不能加载程序。
- 内核已开启：

```text
BPF_SYSCALL BPF_JIT BPF_EVENTS PERF_EVENTS
KPROBE_EVENTS UPROBE_EVENTS TRACEPOINTS TRACING
```

- kprobe/tracepoint/uprobe/trace-printk/perf-output 需要 tracefs。脚本会尝试
  挂载 `/sys/kernel/tracing`，也会回退到 `/sys/kernel/debug/tracing`。
- 不需要 BTF；本仓库内核没有开启 `CONFIG_DEBUG_INFO_BTF`，示例仍可运行。

## 构建与运行

```bash
make
sudo ./run_ebpf.sh all
sudo ./run_ebpf.sh map
sudo ./run_ebpf.sh socket
sudo ./run_ebpf.sh kprobe

sudo ./ebpf_demo map
sudo ./ebpf_demo socket
sudo ./ebpf_demo counter
sudo ./ebpf_demo kprobe
sudo ./ebpf_demo tracepoint
sudo ./ebpf_demo uprobe
sudo ./ebpf_demo trace-printk
sudo ./ebpf_demo perf-output
sudo ./ebpf_demo verifier
```

`run_ebpf.sh` 会检查 root、确保 tracefs 可用、构建程序并汇总：

```text
=== map ===
...
[PASS] map

=== kprobe ===
ebpf_demo_vfs_read count: 512
[PASS] kprobe

Passed: 9  Skipped: 0  Failed: 0
```

退出码约定：`0` 成功，`1` 失败，`2` 当前环境不支持（脚本记为 `SKIP`）。

## 实现说明

- `bpf_insn.h` 提供 MOV/ALU/LDX/STX/ST/JMP/CALL/EXIT 指令宏、
  `emit_ld_imm64()`、`emit_ld_map_fd()` 和原子 `XADD`。
- `bpf_sys.h` 封装 `bpf(2)` 的 map/prog 操作、`BPF_OBJ_GET_INFO_BY_FD`、
  tracefs 读写、`perf_event_open` 和 `PERF_EVENT_IOC_SET_BPF`。
- kprobe/tracepoint/uprobe 通过 `perf_event_open` + `PERF_EVENT_IOC_SET_BPF`
  挂载；程序通过 array map 计数。
- `perf-output` 固定使用 CPU 0，创建 `PERF_COUNT_SW_BPF_OUTPUT` perf event，
  在 BPF 栈上构造 `comm[16] + pid`，用户态从 perf ring buffer 解析
  `PERF_SAMPLE_RAW`。
- `verifier` 故意加载非法程序，必须被拒绝并打印 verifier 日志。

## 清理

每个模式在退出前关闭 map/prog/perf fd，删除动态 kprobe/uprobe 事件；
`trace-printk` 恢复原来的 `tracing_on`。`run_ebpf.sh all` 结束后
`kprobe_events`/`uprobe_events` 应为空，`tracing_on` 保持原值。
