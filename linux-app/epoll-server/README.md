# epoll-server

演示基于 `epoll` 的非阻塞 TCP echo 服务和配套客户端。

## 运行

```bash
make
./epoll_server server 18080
# 另一个终端
./epoll_server client 18080
```

也可以直接运行 `../run-all.sh`，脚本会自动选择端口、启动服务端并完成一次回显。

## 关键点

- 监听 socket 用 `O_NONBLOCK` 设为非阻塞，`accept4(SOCK_NONBLOCK)` 直接得到非阻塞连接。
- `epoll_create1()` 创建实例，`epoll_ctl()` 注册监听 fd 和客户端 fd，`epoll_wait()` 等待事件。
- 采用水平触发（默认模式）：每次事件只读一次，未读完的数据会再次触发。
- 客户端带重试连接，便于在服务端刚启动或 guest 启动较慢时使用。
- 示例面向小报文，收到数据后直接回写；生产代码通常需要处理部分写和 `EPOLLOUT`。

## 预期输出

```text
client: sent "hello epoll", received "hello epoll"
epoll: PASS
```
