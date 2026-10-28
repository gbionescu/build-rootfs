#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bio.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/numa.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#define VBLK_NAME "vblk"
#define VBLK_SIZE (16 * 1024 * 1024)
#define VBLK_QUEUE_DEPTH 64

struct vblk_dev {
    struct blk_mq_tag_set tag_set;
    struct gendisk *disk;
    u8 *data;
    unsigned int major;
    spinlock_t lock;
};

static struct vblk_dev *vblk;

static blk_status_t vblk_queue_rq(struct blk_mq_hw_ctx *hctx,
                                  const struct blk_mq_queue_data *bd)
{
    struct request *rq = bd->rq;
    struct vblk_dev *dev = rq->q->queuedata;
    struct req_iterator iter;
    struct bio_vec bvec;
    loff_t offset = (loff_t)blk_rq_pos(rq) << SECTOR_SHIFT;
    unsigned int bytes = blk_rq_bytes(rq);
    bool write = rq_data_dir(rq) == WRITE;
    unsigned long flags;

    blk_mq_start_request(rq);

    if (req_op(rq) == REQ_OP_FLUSH) {
        blk_mq_end_request(rq, BLK_STS_OK);
        return BLK_STS_OK;
    }

    if (offset < 0 || (u64)offset + bytes > VBLK_SIZE) {
        blk_mq_end_request(rq, BLK_STS_IOERR);
        return BLK_STS_OK;
    }

    spin_lock_irqsave(&dev->lock, flags);
    rq_for_each_segment(bvec, rq, iter) {
        unsigned int length = bvec.bv_len;
        void *kaddr = kmap_local_page(bvec.bv_page);
        void *buffer = kaddr + bvec.bv_offset;

        if (write)
            memcpy(dev->data + (size_t)offset, buffer, length);
        else
            memcpy(buffer, dev->data + (size_t)offset, length);

        kunmap_local(kaddr);
        offset += length;
    }
    spin_unlock_irqrestore(&dev->lock, flags);

    blk_mq_end_request(rq, BLK_STS_OK);
    return BLK_STS_OK;
}

static const struct blk_mq_ops vblk_mq_ops = {
    .queue_rq = vblk_queue_rq,
};

static const struct block_device_operations vblk_fops = {
    .owner = THIS_MODULE,
};

static int __init vblk_init(void)
{
    struct vblk_dev *dev;
    int ret;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (dev == NULL)
        return -ENOMEM;

    dev->data = vzalloc(VBLK_SIZE);
    if (dev->data == NULL) {
        ret = -ENOMEM;
        goto free_dev;
    }

    spin_lock_init(&dev->lock);

    dev->tag_set.ops = &vblk_mq_ops;
    dev->tag_set.nr_hw_queues = 1;
    dev->tag_set.queue_depth = VBLK_QUEUE_DEPTH;
    dev->tag_set.numa_node = NUMA_NO_NODE;
    dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
    dev->tag_set.driver_data = dev;

    ret = blk_mq_alloc_tag_set(&dev->tag_set);
    if (ret)
        goto free_data;

    dev->disk = blk_mq_alloc_disk(&dev->tag_set, dev);
    if (IS_ERR(dev->disk)) {
        ret = PTR_ERR(dev->disk);
        goto free_tag_set;
    }

    dev->major = register_blkdev(0, VBLK_NAME);
    if (dev->major < 0) {
        ret = dev->major;
        goto free_disk;
    }

    dev->disk->major = dev->major;
    dev->disk->first_minor = 0;
    dev->disk->minors = 1;
    dev->disk->fops = &vblk_fops;
    dev->disk->private_data = dev;
    strscpy(dev->disk->disk_name, "vblk0", DISK_NAME_LEN);
    set_capacity(dev->disk, VBLK_SIZE >> SECTOR_SHIFT);

    ret = add_disk(dev->disk);
    if (ret) {
        unregister_blkdev(dev->major, VBLK_NAME);
        put_disk(dev->disk);
        blk_mq_free_tag_set(&dev->tag_set);
        vfree(dev->data);
        kfree(dev);
        return ret;
    }

    vblk = dev;
    pr_info("registered /dev/vblk0 (%d MiB)\n", VBLK_SIZE / (1024 * 1024));
    return 0;

free_disk:
    put_disk(dev->disk);
free_tag_set:
    blk_mq_free_tag_set(&dev->tag_set);
free_data:
    vfree(dev->data);
free_dev:
    kfree(dev);
    return ret;
}

static void __exit vblk_exit(void)
{
    struct vblk_dev *dev = vblk;

    del_gendisk(dev->disk);
    put_disk(dev->disk);
    blk_mq_free_tag_set(&dev->tag_set);
    unregister_blkdev(dev->major, VBLK_NAME);
    vfree(dev->data);
    kfree(dev);

    pr_info("unregistered /dev/vblk0\n");
}

module_init(vblk_init);
module_exit(vblk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual RAM-backed blk-mq block device");
