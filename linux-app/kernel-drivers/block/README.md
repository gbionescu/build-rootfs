# vblk

内存后端的虚拟块设备示例，基于 blk-mq 实现，注册 `/dev/vblk0`（16 MiB）。

## 实现

- `blk_mq_alloc_tag_set()` + `blk_mq_alloc_disk()` 创建请求队列和磁盘。
- `queue_rq` 回调处理 read/write/flush，把数据在 bio 页和 RAM 缓冲区之间复制。
- `add_disk()` 注册设备，udev 自动创建 `/dev/vblk0`。

## 构建与测试

```bash
../build.sh arm64
sudo ./test_vblk.sh
```

测试脚本会在 `/dev/vblk0` 上创建 ext4、挂载、写入并读回文件，然后卸载并
`rmmod`。设备内容是内存中的，`rmmod` 后丢失。
