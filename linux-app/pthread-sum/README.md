# pthread-sum

演示 POSIX 线程创建、参数传递、互斥锁保护共享变量，以及 `pthread_join()` 等待。

## 运行

```bash
make
./pthread_sum
```

## 关键点

- 每个线程负责一段区间，计算局部和。
- 多个线程更新全局变量时必须用 `pthread_mutex_lock()` / `unlock()` 保护。
- `pthread_create()` 失败时返回错误码而不是设置 `errno`。
- `pthread_join()` 等待线程结束，保证主线程读取到最终结果。

## 预期输出

```text
thread 0: [0, 250000) partial=...
...
pthread sum=500000500000 expected=500000500000
pthread: PASS
```
