# mmap-file

演示用 `mmap()` 把文件映射到内存，像访问数组一样读写文件，并用 `msync()` 持久化。

## 运行

```bash
make
./mmap_file
```

## 关键点

- `mkstemp()` 创建临时文件，`ftruncate()` 设置文件大小。
- `mmap(PROT_READ | PROT_WRITE, MAP_SHARED)` 把文件内容映射到进程地址空间。
- 对映射区域的写入会反映到页缓存，`msync(MS_SYNC)` 确保回写到文件。
- `munmap()` 解除映射，重新打开文件读取以验证数据确实持久化。
- 适合大文件随机访问；小文件顺序读写用 `read()` / `write()` 更简单。

## 预期输出

```text
mmap: wrote and verified 65536 bytes (first=A last=P)
mmap: PASS
```
