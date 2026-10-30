# kernel-trace

通过 tracefs 演示 ftrace 的主要用法，脚本直接读写 `/sys/kernel/tracing`
或 `/sys/kernel/debug/tracing`，不依赖 `trace-cmd`。每个模式独立配置、检查
并清理自己的 tracefs 状态。

## 模式

| 模式 | 说明 |
|------|------|
| `function` | `function_graph`、`set_graph_function`、`max_graph_depth`、函数耗时 |
| `event` | `sched_switch`/`sched_wakeup` tracepoint 和字符串过滤器 |
| `syscall` | `syscalls:sys_enter_getpid` 和 `common_pid` 过滤器 |
| `kprobe` | `vfs_read` 的 kprobe + kretprobe，返回探针读取 `$retval` |
| `uprobe` | 用 ELF 符号偏移对 `trace_target()` 建 uprobe + uretprobe |
| `hist` | `sched_wakeup` hist trigger，按 `comm` 聚合 |
| `synthetic` | `trace_marker` + `ftrace:print` hist + synthetic event |
| `snapshot` | `TRACER_SNAPSHOT` 冻结当前缓冲区 |
| `instance` | tracefs instance 独立跟踪 |
| `pipe` | `trace_pipe` 实时流式读取 |
| `stacktrace` | `stacktrace` trigger 打印调用栈 |

## 前置条件

- root。
- 内核开启 `FTRACE`、`FUNCTION_TRACER`、`FUNCTION_GRAPH_TRACER`、
  `FTRACE_SYSCALLS`、`KPROBES`、`KPROBE_EVENTS`、`UPROBE_EVENTS`、
  `HIST_TRIGGERS`、`SYNTH_EVENTS`、`TRACER_SNAPSHOT`、`STACKTRACE`。
- 本仓库 `build-kernel.sh` 已加入这些选项；已跟踪的预编译镜像没有重新构建，
  旧镜像上缺少特性的模式会显示 `SKIP`。需要完整体验时先执行：

```bash
./build_kernel.sh --arch arm64
```

- RISC-V 的 `HIST_TRIGGERS` 依赖 `ARCH_HAVE_NMI_SAFE_CMPXCHG`，部分配置下
  不可用，此时 `hist`/`synthetic` 会跳过。

## 构建与运行

```bash
make
sudo ./trace_demo.sh all
sudo ./trace_demo.sh function
sudo ./trace_demo.sh event
sudo ./trace_demo.sh syscall
sudo ./trace_demo.sh kprobe
sudo ./trace_demo.sh uprobe
sudo ./trace_demo.sh hist
sudo ./trace_demo.sh synthetic
sudo ./trace_demo.sh snapshot
sudo ./trace_demo.sh instance
sudo ./trace_demo.sh pipe
sudo ./trace_demo.sh stacktrace
```

输出示例：

```text
=== function ===
--- trace sample (vfs_read) ---
...
[PASS] function

=== hist ===
--- sched_wakeup histogram ---
...
[PASS] hist

Passed: 11  Skipped: 0  Failed: 0
```

退出码约定：`0` 成功，`1` 失败，`2` 当前内核不支持（脚本记为 `SKIP`，不视为
失败）。脚本使用 `trap cleanup EXIT`，退出时会关闭 tracing、清除动态探针和
trigger、删除 instance。

## trace_workload

`trace_workload` 是配套的用户态负载：

| 模式 | 说明 |
|------|------|
| `file` | 写/读 `/tmp` 临时文件，触发 `vfs_read`/`vfs_write` |
| `syscalls` | 循环 `getpid()` 和 `openat()`/`read()` |
| `sched` | fork 多个子进程并 `sched_yield`/`usleep`，制造调度事件 |
| `target` | 反复调用不内联的 `trace_target()`，供 uprobe 使用 |
| `marker <path>` | 向 tracefs `trace_marker` 写入 `start`/`end` |
| `offset` | 打印 `trace_target()` 的文件偏移，供 uprobe 使用 |

`offset` 使用 `../common/elf_symbol.h` 解析 ELF，因此不需要 `nm`。

## tracefs 状态

脚本只修改示例需要的事件、过滤器和 trigger，并在退出时恢复：

- `tracing_on` 关闭，`current_tracer` 恢复为 `nop`
- `set_graph_function`、`max_graph_depth` 清空
- 关闭 `sched_switch`、`sched_wakeup`、`sys_enter_getpid`、`ftrace:print`、
  `kmalloc`
- 删除 `demo_vfs_read`、`demo_vfs_read_ret`、`demo_uprobe`、
  `demo_uprobe_ret` 动态探针
- 删除 `demo_latency` synthetic event 和 `instances/demo`
