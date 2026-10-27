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

另有 `test1/hello.c`（最小 Hello World）和 `example-matrix1`（ARM NEON/SVE
矩阵乘法），后者需要 ARM 工具链和 SVE 支持。

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
