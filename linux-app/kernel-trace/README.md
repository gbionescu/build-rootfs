# kernel-trace

通过 tracefs 演示 ftrace 的三种常见用法：`function_graph`、tracepoint 和
kprobe。脚本直接读写 `/sys/kernel/tracing`，不依赖 `trace-cmd`。

## 前置条件

- 以内核配置开启 `FTRACE`、`FUNCTION_TRACER`、`FUNCTION_GRAPH_TRACER`、
  `KPROBES`、`KPROBE_EVENTS` 后重新构建内核（本仓库的 `build-kernel.sh`
  已开启）。
- 以 root 运行，脚本会自动挂载 tracefs。

## 构建与运行

```bash
make
sudo ./trace_demo.sh all       # function + event + kprobe
sudo ./trace_demo.sh function  # function_graph 跟踪 vfs_read
sudo ./trace_demo.sh event     # sched:sched_switch 等 tracepoint
sudo ./trace_demo.sh kprobe    # kprobe 跟踪 vfs_read
```

## 输出

每个模式会打印一段 trace 样本，最后汇总：

```text
=== function ===
--- trace sample (vfs_read) ---
...
[PASS] function

=== event ===
...
[PASS] event

=== kprobe ===
...
[PASS] kprobe

Passed: 3  Failed: 0
```

## 展示的接口

- `current_tracer`、`set_graph_function`、`tracing_on`
- `events/*/enable` tracepoint 开关
- `kprobe_events` 动态 kprobe
- `trace` 缓冲区读取与清理
