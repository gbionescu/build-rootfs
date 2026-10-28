# kernel-drivers

三个 Linux 虚拟设备驱动示例（外部内核模块）：

| 目录 | 设备 | 技术点 |
|------|------|--------|
| `char` | `/dev/vchar` | misc device、`file_operations`、`copy_to/from_user`、ioctl |
| `block` | `/dev/vblk0` | blk-mq、`gendisk`、RAM 后端、bio 读写 |
| `net` | `vnet0` / `vnet1` | `net_device`、`ndo_start_xmit`、`__dev_forward_skb`、netns |

## 构建

模块是 out-of-tree 模块，使用仓库内核源码和对应架构配置构建：

```bash
./build.sh arm64
./build.sh amd64
```

首次构建会执行 `modules_prepare`，不会重新编译完整内核。构建前需要
`kernel/linux-6.1.22` 已存在（由 `kernel/build-kernel.sh` 下载）。

清理：

```bash
./build.sh clean
```

## 加载

模块需要 guest 内有 `insmod`/`rmmod`（`kmod` 包）。Ubuntu rootfs 的构建脚本已
加入 `kmod`。在 guest 内：

```bash
sudo ./char/test_vchar.sh
sudo ./block/test_vblk.sh
sudo ./net/test_vnet.sh
```

也可以手动加载：

```bash
sudo insmod char/vchar.ko
sudo insmod block/vblk.ko
sudo insmod net/vnet.ko
```

内核已启用 `MODULES`，未启用 MODVERSIONS/强制签名，因此不需要签名或重建内核。
