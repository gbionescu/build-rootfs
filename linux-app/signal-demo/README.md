# signal-demo

演示信号处理的两种常见方式：异步处理函数和同步等待。

## 运行

```bash
make
./signal_demo
```

## 关键点

- `sigaction()` 注册信号处理函数，比 `signal()` 更可控。
- 处理函数中只能调用异步信号安全函数，示例只修改 `volatile sig_atomic_t` 标志。
- `sa_mask` 指定处理函数执行期间额外屏蔽的信号，`SA_RESTART` 让被中断的系统调用自动重启。
- 先用 `sigprocmask()` 阻塞 `SIGUSR2`，`raise()` 后信号保持 pending，再用
  `sigwait()` 同步取走，适合事件循环或专门的处理线程。

## 预期输出

```text
sigaction: handler ran for SIGUSR1
sigwait: received SIGUSR2
signal: PASS
```
