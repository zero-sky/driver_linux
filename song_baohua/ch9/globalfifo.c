// 增加等待队列 增加异步通知
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>

#define GLOBALFIFO_SIZE 0x1000
#define MEM_CLEAR 0x1
#define GLOBALFIFO_MAJOR 230

static int globalfifo_major = GLOBALFIFO_MAJOR;
module_param(globalfifo_major, int, S_IRUGO);

// 设备结构体，用于存储设备的私有信息
struct globalfifo_dev {
    struct cdev cdev;
    unsigned int current_len;
    unsigned char mem[GLOBALFIFO_SIZE];
    struct mutex mutex;
    wait_queue_head_t r_wait;   // 读等待队列头
    wait_queue_head_t w_wait;   // 写等待队列头
    struct fasync_struct *async_queue;  // 异步通知结构体
};

struct globalfifo_dev *globalfifo_devp;

// 设备打开
static int globalfifo_open(struct inode *inode, struct file *filp)
{
    filp->private_data = globalfifo_devp;    // 重要
    return 0;
}


// 读函数
static ssize_t globalfifo_read(struct file *filp, char __user *buf, size_t size, loff_t *ppos)
{
    unsigned long p = *ppos;        // 当前的文件偏移
    unsigned int count = size;      // 需要读的字节数
    int ret = 0;
    // 注册的时候，设备专用数据结构指针globalfifo_devp会被注册到private_data上面
    // 此时再读出来
    // 虽然直接读取globalfifo_devp也没啥大问题，但是不推荐，可能涉及到后面的优化改动
    struct globalfifo_dev *dev = filp->private_data; 

    DECLARE_WAITQUEUE(wait, current);   // 定义一个等待队列元素，并让当前线程和其挂载起来
    // 将上面定义的等待队列元素添加到读等待队列头中，此时若其他线程唤起r_wait，也就会唤起本线程
    add_wait_queue(&dev->r_wait, &wait);   
    mutex_lock(&dev->mutex);    // 访问共享空间前互斥

    // 内存无数据，非阻塞立即返回
    // 若是阻塞，则释放CPU和互斥量，等待被等待队列唤醒
    while(dev->current_len == 0) {
        if(filp->f_flags & O_NONBLOCK) {
            ret = -EAGAIN;
            goto out;
        }

        __set_current_state(TASK_INTERRUNPTIBLE);
        mutex_unlock(%dev->mutex);

        schedule(); // 调度其他CPU，当前线程在此停止运行

        // 当schedulue运行完毕，就表示本线程被重新唤醒了
        if(signal_pending(current)) {
            // 但必须先判断是被信号唤醒还是被等待队列唤醒
            ret = -ERESTARTSYS;
            goto out2;
        }

        // 走到这里就可以确定是等待队列唤醒的了
        mutex_lock(&dev->mutex);
    }
    
    if(count > dev->current_len) {
        count = dev->current_len;
    }

    // 内核空间的数据，这里是globalfifo_devp指向内存的数据，复制给用户空间
    if(copy_to_user(buf, dev->mem + p, count)) {
        ret = -EFAULT;
    } else {
        memcpy(dev->mem, dev->mem+count, dev->current_len - count); // 通过重复空间内存复制来移动，效率不高
        dev->current_len -= count;
        ret = count;

        printk(KERN_INFO "read %u bytes, current_len:%d\n", count);

        wake_up_interruptible(&dev->w_ait); // 读完毕，唤醒写队列
    }
out:
    mutex_unlock(&dev->mutex);
out2:
    remove_wait_queue(&dev->r_wait, &wait);
    set_current_state(TASK_RUNNING);
    return ret;
}

// 写函数
static ssize_t globalfifo_write(struct file *filp, const char __user *buf, size_t size, loff_t *ppos)
{
    unsigned long p = *ppos;
    unsigned int count = size;
    int ret = 0;

    struct globalfifo_dev *dev = filp->private_data; 

    DECLARE_WAITQUEUE(wait, current); 
    add_wait_queue(&dev->w_wait, &wait);

    while(dev->current_len == GLOBALFIFO_SIZE) {
        if(filp->f_flags & O_NONBLOCK) {
            ret = -EAGAIN;
            goto out;
        }

        __set_current_state(TASK_INTERRUNPTIBLE);
        mutex_unlock(%dev->mutex);

        schedule(); // 调度其他CPU，当前线程在此停止运行

        // 当schedulue运行完毕，就表示本线程被重新唤醒了
        if(signal_pending(current)) {
            // 但必须先判断是被信号唤醒还是被等待队列唤醒
            ret = -ERESTARTSYS;
            goto out2;
        }

        // 走到这里就可以确定是等待队列唤醒的了
        mutex_lock(&dev->mutex);
    }

    mutex_lock(&dev->mutex); 
    if(count > GLOBALFIFO_SIZE - dev->current_len) {
        count = GLOBALFIFO_SIZE - dev->current_len;
    }

    
    // 除了这里，其他地方可以说和读函数一模一样
    if(copy_from_user(dev->mem + dev->current_len, buf, count)) {
        ret = -EFAULT;
        goto out;
    } else {
        *ppos += count; 
        dev->current_len += count;
        ret = count;

        printk(KERN_INFO "write %u bytes, current_len:%d\n", count, dev->current_len);
        wake_up_interruptible(&dev->r_ait); 
        // 发送信号，async_queue是指针，由async函数设置
        if(dev->async_queue) {
            kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
            printk(KERN_DEBUG "%s kill SIGIO\n", __func__);
        }
    }
out:
    mutex_unlock(&dev->mutex);
out2:
    remove_wait_queue(&dev->w_wait, &wait);
    set_current_state(TASK_RUNNING);
    return ret;
}

//seek 修改文件偏移SEEK_SET:0, SEEK_CUR:1, SEEK_END;2
static loff_t globalfifo_llseek(struct file *filp, loff_t offset, int orig)
{
    loff_t ret = 0;
    switch(orig) 
    {
        // 从文件开头开始偏移
    case 0: if(offset < 0) {
            ret = -EINVAL;
            break;
        }
        if((unsigned int)offset > GLOBALFIFO_SIZE) {
            ret = -EINVAL;
            break;
        }    
        filp->f_pos = (unsigned int)offset;
        ret = filp->f_pos;  //filp的f_pos存放着当前文件的偏移
        break;
    
    // 从文件当前位置开始偏移
    case 1:
        if((filp->f_pos + offset) > GLOBALFIFO_SIZE) {
            ret = -EINVAL;
            break;  
        }
        if((filp->f_pos + offset) < 0) {
            ret = -EINVAL;
            break;  
        }
        filp->f_pos += offset;
        ret = filp->f_pos;  // 返回的是当前偏移值
        break;
    default:
        ret = -EINVAL;  // 错误返回负值
        break;
    }
    return ret;
}

//ioctl
static long globalfifo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct globalfifo_dev *dev = filp->private_data;

    switch(cmd) 
    {
    case MEM_CLEAR:
        mutex_lock(&dev->mutex);
        memset(dev->mem, 0, GLOBALFIFO_SIZE);
        mutex_unlock(&dev->mutex);
        printk(KERN_INFO "globalfifo is set to zero\n");
        break;
    default:
        return -EINVAL;
    }
    return 0;
}


static unsigned int globalfifo_poll(struct file *filp, poll_table *wait)
{
    unsigned int mask = 0;
    struct globalfifo_dev *dev = filp->private_data;

    mutex_lock(&dev->mutex);
    // 核心函数，这里的等待队列元素是调用者提供的，挂载的是调用者的当前线程
    // 此次的功能为，每当读/写完成后，读写函数会唤醒对应的等待队列头
    // 因此，也会唤醒本线程，而本线程是因select而睡眠的线程
    // 从而导致select继续执行而返回。
    poll_wait(filp, &dev->r_wait, wait);
    poll_wait(filp, &dev->w_wait, wait);

    if(dev->current_len != 0) {
        mask |= POLLIN | POLLRDNORM;
    }

    if(dev->current_len != GLOBALFIFO_SIZE) {
        mask |= POLLOUT | POLLWRNORM;
    }

    mutex_unlock(&dev->mutex);
    return mask;
}

static int globalfifo_fasync(int fd, struct file *filp, int mode)
{
    struct globalfifo_dev *dev = filp->private_data;
    return fasync_helper(fd, filp, mode, &dev->async_queue);
}

static int globalfifo_release(struct inode *inode, struct file *filp)
{
    globalfifo_fasync(-1, filp, 0);     // 释放异步通知结构体
    return 0;
}

// 文件操作结构体
static const struct file_operations globalfifo_fops = {
    .owner = THIS_MODULE,
    .llseek = globalfifo_llseek,
    .read = globalfifo_read,
    .write = globalfifo_write,
    .unlocked_ioctl = globalfifo_ioctl,
    .open = globalfifo_open,
    .poll = globalfifo_poll,
};


// 字符设备初始化
static void globalfifo_setup_cdev(struct globalfifo_dev *dev, int index)
{
    int err;
    int devno = MKDEV(globalfifo_major, index);  // 主设备号固定，次设备号参数，生成一个设备号

    cdev_init(&dev->cdev, &globalfifo_fops);     // 初始化字符设备结构体
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);       // 注册字符设备
    if(err) {
        printk(KERN_NOTICE "Error %d adding globalfifo%d",err, index);
    }
}

// 模块加载初始化函数
static int __init globalfifo_init(void)
{
    int ret;
    dev_t devno = MKDEV(globalfifo_major, 0);    // 生产一个次设备号为0的设备号

    // 如果存在主设备号，则注册该设备号的设备；否则就动态分配一个
    if(globalfifo_major) {
        ret = register_chrdev_region(devno, 1, "globalfifo");
    } else {
        ret = alloc_chrdev_region(&devno, 0, 1, "globalfifo");
        globalfifo_major = MAJOR(devno);
    }

    if(ret < 0) {
        return ret;
    }

    // 申请一个空间用于存放设备的特定信息
    globalfifo_devp = kzalloc(sizeof(struct globalfifo_dev), GFP_KERNEL);
    if(!globalfifo_devp) {
        ret = -ENOMEM;
        goto fail_malloc;
    }
    globalfifo_setup_cdev(globalfifo_devp, 0);

    mutex_init(&globalfifo_devp->mutex);     // 初始化之
    init_waitqueue_head(&globalfifo_devp->r_wait);
    init_waitqueue_head(&globalfifo_devp->w_wait);
    return 0;

fail_malloc:
    unregister_chrdev_region(devno, 1);
    return ret;
}
module_init(globalfifo_init);

// 卸载函数
static void __exit globalfifo_exit(void)
{
    cdev_del(&globalfifo_devp->cdev);
    kfree(globalfifo_devp);
    unregister_chrdev_region(MKDEV(globalfifo_major, 0), 1);
}
module_exit(globalfifo_exit);

MODULE_AUTHOR("Barry Song <baohua@kernel.org");
MODULE_LICENSE("GPL v2");
