# process-fork

演示 `fork()` 创建子进程，以及父进程用 `waitpid()` 回收子进程并检查退出码。

## 运行

```bash
make
./fork_demo
```

## 关键点

- `fork()` 在父进程中返回子进程 PID，在子进程中返回 0。
- 子进程使用 `_exit()` 退出，避免刷新继承自父进程的 stdio 缓冲区。
- 父进程必须 `waitpid()` 回收子进程，否则会产生僵尸进程。
- `WIFEXITED` / `WEXITSTATUS` 用于判断子进程是否正常退出及其退出码。

## 预期输出

```text
child 0: pid=..., ppid=...
...
parent: child 0 exited with code 10
...
fork: PASS
```
