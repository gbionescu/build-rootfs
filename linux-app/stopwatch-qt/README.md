# stopwatch-qt

Qt6 Widgets 实现的图形界面秒表，支持 Start/Stop、Reset 和 Lap 计次。

## 功能

- 大字体显示 `HH:MM:SS.mmm`，每 30 ms 刷新一次。
- `Start` / `Stop` 切换计时，`Reset` 清零并清空计次。
- `Lap` 记录当前总时间，显示在下方列表。
- 键盘快捷键：`Space` 开始/停止，`R` 复位，`L` 计次，`Esc` 退出。
- 使用 `QElapsedTimer` 累计真实经过时间，避免界面刷新延迟造成误差。

## 依赖

Ubuntu rootfs 由 `type-ubuntu22.04` / `type-ubuntu24.04` 构建时会自动安装：

- `qt6-base-dev`、`g++`、`make`、`pkg-config`
- `xserver-xorg-core`、`xserver-xorg-input-libinput`、`xinit`、`xauth`
- `openbox`、`fonts-dejavu-core`、`libgl1-mesa-dri`

## 构建

```bash
make
# 或者不依赖 make：
./build.sh
```

## 在 QEMU guest 中运行

先构建带 GUI 依赖的 rootfs：

```bash
sudo ./build-rootfs.sh ubuntu24.04 --arch arm64
```

然后用 SDL 窗口或 VNC 启动 QEMU：

```bash
./run-qemu.sh rootfs_ubuntu24.04_arm64.ext4 --arch arm64 --gui
# 或者无显示器环境：
./run-qemu.sh rootfs_ubuntu24.04_arm64.ext4 --arch arm64 --vnc 1
# 宿主机执行：
#   ssh -L 5901:127.0.0.1:5901 <server>
#   vncviewer 127.0.0.1:5901
```

在 guest 的串口终端登录后启动 Xorg 和秒表：

```bash
/mnt/linux-app/stopwatch-qt/start-gui.sh
```

QEMU 窗口（或 VNC 画面）会显示秒表界面，串口终端仍可用于查看内核和 Xorg 日志。

## 在宿主机直接运行

宿主机安装 `qt6-base-dev` 后：

```bash
make
./stopwatch
```

## 常见问题

- `Xorg` 报 `no screens found`、`/dev/dri/card0: No such file or directory`：
  QEMU 启动时没有添加虚拟 GPU。默认的 `-nographic` 模式不会创建显示设备，请用
  `--gui` 或 `--vnc <display>` 重新启动 QEMU。`start-gui.sh` 会提前检查并给出
  提示。
- QEMU/VNC 窗口短暂黑屏：guest 内 Qt 首次绘制在纯软件模拟下可能需要约 10 秒，
  窗口随后会正常显示。
- 运行 `--gui` 后完全看不到窗口：`--gui` 的 SDL 窗口属于运行 QEMU 的桌面会话。
  用 `sudo` 启动时窗口可能落在无法访问的会话里。可以：
  1. 改用 `--vnc <display>`，用 VNC 客户端连接；或
  2. `sudo chown $USER rootfs_arm64.ext4` 后以桌面用户身份直接运行
     `./run-qemu.sh`。
