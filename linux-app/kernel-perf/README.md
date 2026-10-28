# kernel-perf

用 `perf_event_open(2)` 直接统计内核 perf 事件，不依赖 `perf` 命令行工具。

## 事件

- 软件事件：`task-clock`、`context-switches`、`page-faults`
- 硬件事件：`cycles`、`instructions`、`branch-misses`、`cache-misses`

硬件 PMU 不可用时，对应事件会被标记为 `unsupported`，软件事件仍然有效。
QEMU TCG 下通常没有完整硬件 PMU，这是正常现象。

## 构建与运行

```bash
make
sudo ./perf_counter                 # all，500ms
sudo ./perf_counter cpu 1000
sudo ./perf_counter syscall 500
sudo ./perf_counter memory 500
```

`perf_event_open()` 需要 root，或把 `kernel.perf_event_paranoid` 调低。

## 输出示例

```text
mode: cpu, requested: 1000 ms, wall: 1000 ms
event                            count
task-clock                  1000123456
context-switches                    12
page-faults                        120
cycles                        9876543210
instructions                  4567890123
IPC (instructions/cycles): 0.46
unsupported: cache-misses
```

## 展示的接口

- `perf_event_attr` 的 `type` / `config`
- `syscall(__NR_perf_event_open)`
- `PERF_EVENT_IOC_RESET/ENABLE/DISABLE`
- `read()` 读取计数器
