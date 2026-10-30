# linux-app demos

这个目录默认通过 QEMU 9p 共享到 guest 的 `/mnt/linux-app`，用于演示 Linux
应用层开发中的关键技术。命令行示例都是自校验的：退出码为 0 表示检查通过，并打印
`PASS`；GUI 示例见各自目录的 README。

## 示例

| 目录 | 技术点 | 关键 API |
|------|--------|----------|
| `process-fork` | 创建子进程并回收 | `fork`, `waitpid`, `_exit` |
| `pthread-sum` | 多线程与互斥锁 | `pthread_create`, `pthread_mutex_lock` |
| `pipe-ipc` | 匿名管道与命名管道 | `pipe`, `mkfifo`, `read`, `write` |
| `shm-sem` | 共享内存与跨进程信号量 | `shm_open`, `mmap`, `sem_init`, `sem_wait` |
| `signal-demo` | 异步信号与同步等待 | `sigaction`, `raise`, `sigwait` |
| `epoll-server` | 非阻塞 TCP echo 服务 | `socket`, `epoll_wait`, `accept4` |
| `mmap-file` | 内存映射文件读写 | `mmap`, `msync`, `munmap` |
| `stopwatch-qt` | Qt6 图形界面秒表 | `QApplication`, `QTimer`, `QElapsedTimer` |
| `snake-tk` | Python/Tkinter 贪吃蛇 | `tkinter.Canvas`, `after`, 键盘事件 |
| `kernel-perf` | 8 种 perf_event_open 用法 | 计数、分组、采样、tracepoint、kprobe/uprobe、PID/CPU |
| `kernel-trace` | 11 种 ftrace/tracefs 用法 | function_graph、tracepoint、kprobe/uprobe、hist/synthetic、snapshot、instance、trace_pipe |
| `ebpf` | 9 种裸 bpf(2) 用法 | map、socket filter、kprobe、tracepoint、uprobe、trace_printk、perf_event_output、verifier |
| `common/elf_symbol.h` | ELF 符号到文件偏移 | PIE/非 PIE、32/64 位符号表解析 |
| `kernel-drivers` | 虚拟字符/块/网络设备 | misc device, blk-mq, net_device |

另有 `test1/hello.c`（最小 Hello World）和 `example-matrix1`（ARM NEON/SVE
矩阵乘法），后者需要 ARM 工具链和 SVE 支持。

## perf / trace / eBPF 方面对照

| 方面 | perf (`kernel-perf`) | trace (`kernel-trace`) | eBPF (`ebpf`) |
|------|----------------------|------------------------|----------------|
| 基础计数/聚合 | `stat`, `group` | — | `map`, `counter` |
| 采样/事件输出 | `sample`, `pid`, `cpu` | — | `perf-output` |
| tracepoint | `tracepoint` | `event`, `syscall` | `tracepoint` |
| kprobe | `kprobe` | `kprobe` | `kprobe` |
| uprobe | `uprobe` | `uprobe` | `uprobe` |
| 过滤/直方图/合成事件 | — | `hist`, `synthetic` | — |
| 函数图/调用栈 | `sample` callchain | `function`, `stacktrace` | `trace-printk` |
| 快照/实例/实时流 | — | `snapshot`, `instance`, `pipe` | — |
| socket/网络 | — | — | `socket` |
| verifier/程序安全 | — | — | `verifier` |

三个套件都需要 root；分别运行：

```bash
sudo ./kernel-perf/run_perf.sh all
sudo ./kernel-trace/trace_demo.sh all
sudo ./ebpf/run_ebpf.sh all
```

## 在宿主机构建

```bash
make            # 编译全部示例
make test       # 等同 ./run-all.sh
make gui        # 编译 Qt6 秒表（需要 qt6-base-dev）
make clean
```

也可以只构建一个示例：

```bash
make -C process-fork
./process-fork/fork_demo
```

## 在 guest 内运行

Ubuntu rootfs 里没有安装 `make`，但 `run-all.sh` 直接调用 `gcc` 编译，因此
可以直接在 guest 内运行：

```bash
cd /mnt/linux-app
./run-all.sh
```

单独编译某个示例：

```bash
gcc -Wall -Wextra -O2 -D_GNU_SOURCE process-fork/fork_demo.c -o /tmp/fork_demo
/tmp/fork_demo
```

`pthread-sum`、`shm-sem` 需要 `-pthread`，`shm-sem` 还需要 `-lrt`；`epoll-server`
是客户端/服务端模型，需要分别启动：

```bash
./epoll-server/epoll_server server 18080
./epoll-server/epoll_server client 18080
```

## Qt6 图形界面秒表

`stopwatch-qt/` 是 Qt6 Widgets 秒表。Ubuntu rootfs 的 GUI 依赖由
`type-ubuntu24.04` / `type-ubuntu22.04` 构建脚本自动安装。构建 rootfs 后使用
`run-qemu.sh --gui`（SDL 窗口）或 `--vnc <display>` 启动图形显示：

```bash
sudo ./build-rootfs.sh ubuntu24.04 --arch arm64
./run-qemu.sh rootfs_ubuntu24.04_arm64.ext4 --arch arm64 --gui
```

登录 guest 后运行：

```bash
/mnt/linux-app/stopwatch-qt/start-gui.sh
```

详见 [stopwatch-qt/README.md](stopwatch-qt/README.md)。

## Python/Tkinter 贪吃蛇

`snake-tk/` 使用 Python 标准库 Tkinter 实现贪吃蛇。Ubuntu rootfs 的
`python3-tk` 依赖由构建脚本自动安装；已有镜像可手动执行
`apt-get install -y python3-tk`。在 guest 的图形环境中运行：

```bash
/mnt/linux-app/snake-tk/start-snake.sh
```

如果 Xorg 已在运行，也可以直接复用当前显示：

```bash
DISPLAY=:0 python3 /mnt/linux-app/snake-tk/snake.py
```

逻辑单元测试不依赖显示器：

```bash
cd snake-tk && python3 -m unittest -v test_snake.py
```

详见 [snake-tk/README.md](snake-tk/README.md)。

## eBPF 示例

`ebpf/` 直接调用 `bpf(2)` 并手写 eBPF 指令，不依赖 clang/libbpf/BCC/bpftrace，
覆盖 map、socket filter、kprobe、tracepoint、uprobe、`bpf_trace_printk`、
`bpf_perf_event_output` 和 verifier。需要 root；tracing 模式还需要 tracefs：

```bash
make -C ebpf
sudo ./ebpf/run_ebpf.sh all
sudo ./ebpf/ebpf_demo kprobe
```

当前内核已包含所需 BPF 配置，无需 BTF，也不需要重建内核。详见
[ebpf/README.md](ebpf/README.md)。

## kernel perf 示例

`kernel-perf/` 直接调用 `perf_event_open(2)`，覆盖计数、分组、采样、
tracepoint、kprobe、uprobe、按 PID 和按 CPU 统计，不依赖 `perf` 命令。
需要 root 或降低 `kernel.perf_event_paranoid`：

```bash
make -C kernel-perf
sudo ./kernel-perf/run_perf.sh all
sudo ./kernel-perf/perf_demo stat 500
sudo ./kernel-perf/perf_demo sample 500
```

硬件 PMU 在 QEMU TCG 下可能不可用，`stat` 会跳过硬件事件并继续统计软件事件。
uprobe 通过 `common/elf_symbol.h` 解析自身符号，不需要 `nm`。详见
[kernel-perf/README.md](kernel-perf/README.md)。

## kernel trace 示例

`kernel-trace/` 通过 tracefs 演示 11 种用法：`function_graph`、tracepoint
过滤器、syscall 过滤、kprobe/kretprobe、uprobe/uretprobe、hist trigger、
synthetic event、snapshot、instance、`trace_pipe` 和 stacktrace。需要内核开启
相应 tracing 配置并重新构建（本仓库 `build-kernel.sh` 已开启）。在 guest 内
以 root 运行：

```bash
make -C kernel-trace
sudo ./kernel-trace/trace_demo.sh all
```

也可以从 `linux-app` 目录运行 `make test-perf` / `make test-trace` / `make test-ebpf`。这几个
示例需要 root 和目标内核特性，因此没有加入 `run-all.sh`；旧内核缺少的特性会
显示 `SKIP`。详见 [kernel-trace/README.md](kernel-trace/README.md)。

## kernel 虚拟设备驱动

`kernel-drivers/` 包含三个外部内核模块：

- `char/vchar`：misc 字符设备 `/dev/vchar`，4KB 缓冲区，read/write/ioctl。
- `block/vblk`：16 MiB 内存后端 blk-mq 块设备 `/dev/vblk0`。
- `net/vnet`：veth 风格 `vnet0`/`vnet1` 设备对，支持 netns 内 ping。

构建（默认 arm64）并使用 `kmod` 的 `insmod` 加载：

```bash
./kernel-drivers/build.sh arm64
sudo ./kernel-drivers/char/test_vchar.sh
sudo ./kernel-drivers/block/test_vblk.sh
sudo ./kernel-drivers/net/test_vnet.sh
```

模块是 out-of-tree 构建，不修改内核镜像；rootfs 构建脚本已加入 `kmod`。
