# vchar

misc 字符设备示例，注册 `/dev/vchar`，内部维护一个 4KB 缓冲区。

## 接口

- `write()`：把用户数据复制到内核缓冲区（最多 4KB）。
- `read()`：从当前文件偏移读取缓冲区，读完返回 0。
- `ioctl(VCHAR_IOC_RESET)`：清空缓冲区。
- `ioctl(VCHAR_IOC_STATS)`：读取读写次数和当前长度。

## 构建与测试

```bash
../build.sh arm64
sudo ./test_vchar.sh
```

测试脚本会 `insmod vchar.ko`、写入并读回字符串、然后 `rmmod`。
