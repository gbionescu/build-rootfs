# shm-sem

演示 POSIX 共享内存配合信号量实现跨进程同步。

## 运行

```bash
make
./shm_sem
```

## 关键点

- `shm_open()` 创建共享内存对象，`ftruncate()` 设置大小。
- `mmap(MAP_SHARED)` 把共享内存映射到进程地址空间，`fork()` 后父子进程看到同一块内存。
- `sem_init(&sem, 1, 0)` 的 `pshared=1` 表示信号量可跨进程使用。
- 子进程写完数据后 `sem_post()`，父进程 `sem_wait()` 后再读取，避免数据竞争。
- 清理顺序：`sem_destroy()`、`munmap()`、`shm_unlink()`。

## 预期输出

```text
shm: value=42 message="written by child ..."
shm-sem: PASS
```
