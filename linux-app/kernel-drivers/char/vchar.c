#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define VCHAR_BUFSIZE 4096

#define VCHAR_IOC_MAGIC 'V'
#define VCHAR_IOC_RESET _IO(VCHAR_IOC_MAGIC, 0)
#define VCHAR_IOC_STATS _IOR(VCHAR_IOC_MAGIC, 1, struct vchar_stats)

struct vchar_stats {
    __u64 reads;
    __u64 writes;
    __u32 length;
};

struct vchar_dev {
    struct mutex lock;
    char *buffer;
    size_t length;
    u64 reads;
    u64 writes;
};

static struct vchar_dev vchar;

static int vchar_open(struct inode *inode, struct file *file)
{
    file->private_data = &vchar;
    return 0;
}

static ssize_t vchar_read(struct file *file, char __user *user_buffer,
                          size_t count, loff_t *ppos)
{
    struct vchar_dev *dev = file->private_data;
    ssize_t ret = 0;

    mutex_lock(&dev->lock);

    if (*ppos >= dev->length)
        goto out;

    if (count > dev->length - *ppos)
        count = dev->length - *ppos;

    if (copy_to_user(user_buffer, dev->buffer + *ppos, count)) {
        ret = -EFAULT;
        goto out;
    }

    *ppos += count;
    dev->reads++;
    ret = count;

out:
    mutex_unlock(&dev->lock);
    return ret;
}

static ssize_t vchar_write(struct file *file, const char __user *user_buffer,
                           size_t count, loff_t *ppos)
{
    struct vchar_dev *dev = file->private_data;
    ssize_t ret = count;

    mutex_lock(&dev->lock);

    if (count > VCHAR_BUFSIZE)
        count = VCHAR_BUFSIZE;

    if (copy_from_user(dev->buffer, user_buffer, count)) {
        ret = -EFAULT;
        goto out;
    }

    dev->length = count;
    dev->writes++;
    *ppos = 0;

out:
    mutex_unlock(&dev->lock);
    return ret;
}

static long vchar_ioctl(struct file *file, unsigned int command,
                        unsigned long argument)
{
    struct vchar_dev *dev = file->private_data;

    switch (command) {
    case VCHAR_IOC_RESET:
        mutex_lock(&dev->lock);
        dev->length = 0;
        mutex_unlock(&dev->lock);
        return 0;
    case VCHAR_IOC_STATS: {
        struct vchar_stats stats;

        mutex_lock(&dev->lock);
        stats.reads = dev->reads;
        stats.writes = dev->writes;
        stats.length = dev->length;
        mutex_unlock(&dev->lock);

        if (copy_to_user((void __user *)argument, &stats, sizeof(stats)))
            return -EFAULT;
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static const struct file_operations vchar_fops = {
    .owner = THIS_MODULE,
    .open = vchar_open,
    .read = vchar_read,
    .write = vchar_write,
    .unlocked_ioctl = vchar_ioctl,
};

static struct miscdevice vchar_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "vchar",
    .fops = &vchar_fops,
};

static int __init vchar_init(void)
{
    int ret;

    mutex_init(&vchar.lock);
    vchar.buffer = kmalloc(VCHAR_BUFSIZE, GFP_KERNEL);
    if (vchar.buffer == NULL)
        return -ENOMEM;

    ret = misc_register(&vchar_misc);
    if (ret) {
        kfree(vchar.buffer);
        return ret;
    }

    pr_info("registered /dev/vchar (%d bytes)\n", VCHAR_BUFSIZE);
    return 0;
}

static void __exit vchar_exit(void)
{
    misc_deregister(&vchar_misc);
    kfree(vchar.buffer);
    pr_info("unregistered /dev/vchar\n");
}

module_init(vchar_init);
module_exit(vchar_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Virtual character device with a 4KB buffer");
