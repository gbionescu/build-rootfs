# snake-tk

Python + Tkinter 实现的贪吃蛇，复用仓库已有的 Xorg/openbox 图形环境。

## 功能

- 20×20 网格，方向键或 WASD 控制。
- 吃到食物 +10 分并增长，每 50 分提升一级速度。
- `Space` / `P` 暂停或继续，`R` 重开，`Esc` / `Q` 退出。
- 撞墙或撞到自身游戏结束，显示最终分数。

## 依赖

Ubuntu rootfs 由 `type-ubuntu22.04` / `type-ubuntu24.04` 构建时会自动安装
`python3-tk`。已有的镜像也可以手动安装：

```bash
apt-get update
apt-get install -y python3-tk
```

## 运行

先启动带图形设备的 QEMU（`--gui` 或 `--vnc`），登录 guest 后：

```bash
/mnt/linux-app/snake-tk/start-snake.sh
```

如果 guest 里已经有 Xorg 在运行，可以直接在现有显示上启动：

```bash
DISPLAY=:0 python3 /mnt/linux-app/snake-tk/snake.py
```

宿主机安装 `python3-tk` 后也可以直接运行：

```bash
python3 snake.py
```

## 测试

逻辑层不依赖显示器，可直接运行单元测试：

```bash
python3 -m unittest -v test_snake.py
```
