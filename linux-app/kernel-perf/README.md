# kernel-perf

用 `perf_event_open(2)` 直接统计、分组和采样内核 perf 事件，不依赖 `perf`
命令行工具。示例覆盖计数、分组、采样、tracepoint、kprobe、uprobe、按 PID
和按 CPU 统计。

## 模式

| 模式 | 说明 |
|------|------|
| `stat` | 软件/硬件计数器、IPC、unsupported 事件 |
| `group` | `PERF_FORMAT_GROUP` 分组读取、`time_enabled/time_running` 缩放 |
| `sample` | mmap 环形缓冲区、`PERF_RECORD_SAMPLE`、IP/TID/TIME/CPU/CALLCHAIN |
| `tracepoint` | 从 tracefs 读取 tracepoint id，打开 `PERF_TYPE_TRACEPOINT` |
| `kprobe` | 创建 `vfs_read` kprobe，再用 perf 统计 |
| `uprobe` | 解析自身 ELF 符号，对 `uprobe_target()` 建 uprobe |
| `pid` | 只统计 fork 出来的子进程 |
| `cpu` | 系统级统计 CPU 0 上的所有任务 |

## 前置条件

- root，或降低 `kernel.perf_event_paranoid`（`tracepoint`/`kprobe`/`uprobe`
  还需要 tracefs）。
- 内核开启 `CONFIG_PERF_EVENTS`；kprobe/uprobe 需要 `CONFIG_KPROBE_EVENTS`、
  `CONFIG_UPROBE_EVENTS`。
- QEMU TCG 下通常没有硬件 PMU，`cycles`、`instructions` 等会标记为
  `unsupported`，软件事件和采样仍然可用。

本仓库 `build-kernel.sh` 已为各架构打开上述 trace 选项。已跟踪的预编译内核
镜像没有重新构建，因此使用旧镜像时部分模式会显示 `SKIP`。需要完整体验时
先执行：

```bash
./build_kernel.sh --arch arm64
```

## 构建与运行

```bash
make
sudo ./run_perf.sh all
sudo ./run_perf.sh stat
sudo ./run_perf.sh sample

sudo ./perf_demo stat 500
sudo ./perf_demo group
sudo ./perf_demo sample 500
sudo ./perf_demo tracepoint
sudo ./perf_demo kprobe
sudo ./perf_demo uprobe
sudo ./perf_demo pid
sudo ./perf_demo cpu
```

`run_perf.sh` 会检查 root、挂载 tracefs、构建程序并汇总结果：

```text
=== stat ===
...
[PASS] stat

=== group ===
...
[PASS] group

Passed: 8  Skipped: 0  Failed: 0
```

退出码约定：`0` 成功，`1` 失败，`2` 当前内核或 QEMU 不支持（脚本记为
`SKIP`，不视为失败）。

## 采样输出

`sample` 使用 `PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
PERF_SAMPLE_CPU | PERF_SAMPLE_CALLCHAIN`，打印前 5 个样本及调用链。没有安装
`perf` 时也可以把 IP 交给 `addr2line`：

```bash
addr2line -e ./perf_demo -f -C 0x...
```

## uprobe 符号解析

`perf_demo` 通过 `../common/elf_symbol.h` 读取 `/proc/self/exe` 的
`SHT_SYMTAB`/`SHT_DYNSYM`，再用 `PT_LOAD` 把符号地址转换为文件偏移。PIE 和
非 PIE 二进制都支持，因此不需要 `nm` 或 `perf probe`。
