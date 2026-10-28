#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#define VNET_COUNT 2

struct vnet_priv {
    struct net_device *peer;
};

static struct net_device *vnet_devices[VNET_COUNT];

static netdev_tx_t vnet_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct vnet_priv *priv = netdev_priv(dev);
    struct net_device *peer = priv->peer;
    int ret;

    if (peer == NULL || !netif_running(peer)) {
        dev->stats.tx_dropped++;
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    dev->stats.tx_packets++;
    dev->stats.tx_bytes += skb->len;

    skb->dev = peer;
    ret = __dev_forward_skb(peer, skb);
    if (ret) {
        dev->stats.tx_errors++;
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    peer->stats.rx_packets++;
    peer->stats.rx_bytes += skb->len;
    netif_rx(skb);

    return NETDEV_TX_OK;
}

static int vnet_open(struct net_device *dev)
{
    netif_start_queue(dev);
    return 0;
}

static int vnet_stop(struct net_device *dev)
{
    netif_stop_queue(dev);
    return 0;
}

static const struct net_device_ops vnet_netdev_ops = {
    .ndo_open = vnet_open,
    .ndo_stop = vnet_stop,
    .ndo_start_xmit = vnet_xmit,
};

static int __init vnet_init(void)
{
    int i;
    int ret;

    for (i = 0; i < VNET_COUNT; i++) {
        struct net_device *dev;
        char name[IFNAMSIZ];

        dev = alloc_etherdev(sizeof(struct vnet_priv));
        if (dev == NULL) {
            ret = -ENOMEM;
            goto unregister;
        }

        eth_hw_addr_random(dev);
        dev->netdev_ops = &vnet_netdev_ops;
        snprintf(name, sizeof(name), "vnet%d", i);
        strscpy(dev->name, name, IFNAMSIZ);

        ret = register_netdev(dev);
        if (ret) {
            free_netdev(dev);
            goto unregister;
        }

        vnet_devices[i] = dev;
    }

    ((struct vnet_priv *)netdev_priv(vnet_devices[0]))->peer = vnet_devices[1];
    ((struct vnet_priv *)netdev_priv(vnet_devices[1]))->peer = vnet_devices[0];

    pr_info("registered vnet0 <-> vnet1\n");
    return 0;

unregister:
    while (--i >= 0) {
        unregister_netdev(vnet_devices[i]);
        free_netdev(vnet_devices[i]);
        vnet_devices[i] = NULL;
    }
    return ret;
}

static void __exit vnet_exit(void)
{
    int i;

    for (i = 0; i < VNET_COUNT; i++) {
        struct vnet_priv *priv = netdev_priv(vnet_devices[i]);

        priv->peer = NULL;
        unregister_netdev(vnet_devices[i]);
        free_netdev(vnet_devices[i]);
        vnet_devices[i] = NULL;
    }

    pr_info("unregistered vnet0 and vnet1\n");
}

module_init(vnet_init);
module_exit(vnet_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual Ethernet device pair");
