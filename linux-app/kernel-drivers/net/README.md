# vnet

veth 风格的虚拟网络设备对：加载模块后创建 `vnet0` 和 `vnet1`。
在一个设备上发送的以太网帧会被转发到对端的接收路径。

## 实现

- `alloc_etherdev()` 创建两个 Ethernet net_device。
- `ndo_start_xmit` 调用 `__dev_forward_skb()` 和 `netif_rx()` 把 skb 交给对端。
- 支持把其中一个设备移动到 network namespace，配合 `ip netns` 测试。

## 构建与测试

```bash
../build.sh arm64
sudo ./test_vnet.sh
```

测试脚本把 `vnet1` 放进独立 netns，配置 `10.0.0.1/24` 和 `10.0.0.2/24`，
然后用 ping 验证双向收发，最后恢复并 `rmmod`。

注意：不要把 `vnet1` 留在 netns 中直接删除 namespace 后卸载模块；先把设备移回
init netns，再删除 namespace 和 `rmmod`。
