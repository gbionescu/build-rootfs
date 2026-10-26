# pipe-ipc

演示两种进程间通信方式：匿名管道 `pipe()` 和命名管道 `mkfifo()`。

## 运行

```bash
make
./pipe_ipc
```

## 关键点

- `pipe()` 创建一对读写文件描述符，通常配合 `fork()` 在父子进程间传递数据。
- 管道是字节流，没有消息边界；示例约定一次读取全部内容。
- `mkfifo()` 在文件系统中创建命名管道，无关进程也可以通过路径打开它。
- 使用完命名管道后要 `unlink()` 删除，避免残留在 `/tmp`。

## 预期输出

```text
pipe: received "hello from pipe"
fifo: received "hello from fifo"
pipe-ipc: PASS
```
