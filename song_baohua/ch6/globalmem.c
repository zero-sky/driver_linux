// 第一个驱动
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define GLOBALMEM_SIZE 0x1000
#define MEM_CLEAR 0x1
#define GLOBALMEM_MAJOR 230

static int globalmen_major = GLOBALMEM_MAJOR;
module_param(globalmen_major, int, S_IRUGO);

// 设备结构体，用于存储设备的私有信息
struct globalmem_dev {
    struct cdev cdev;
    unsigned char mem[GLOBALMEM_SIZE];
};

struct globalmem_dev *globalmem_devp;

// 设备打开
static int globalmem_open(struct inode *inode, struct file *filp)
{
    filp->private_data = globalmem_devp;    // 重要
    return 0;
}


// 读函数
static ssize_t globalmem_read(struct file *filp, char __user *buf, size_t size, loff_t *ppos)
{
    unsigned long p = *ppos;        // 当前的文件偏移
    unsigned int count = size;      // 需要读的字节数
    int ret = 0;
    // 注册的时候，设备专用数据结构指针globalmem_devp会被注册到private_data上面
    // 此时再读出来
    // 虽然直接读取globalmem_devp也没啥大问题，但是不推荐，可能涉及到后面的优化改动
    struct globalmem_dev *dev = filp->private_data; 

    // 数据可信判断
    if(p >= GLOBALMEM_SIZE) {
        return 0;
    }
    if(count > GLOBALMEM_SIZE - p){
        count = GLOBALMEM_SIZE - p;
    }

    // 内核空间的数据，这里是globalmem_devp指向内存的数据，复制给用户空间
    if(copy_to_user(buf, dev->mem + p, count)) {
        ret = -EFAULT;
    } else {
        *ppos += count;     // 当前偏移值，这里可以看出，文件的偏移值是由系统保存的，用户无需自行处理
        ret = count;

        printk(KERN_INFO "read %u bytes from %lu\n", count, p);
    }

    return ret;
}

// 写函数
static ssize_t globalmem_write(struct file *filp, const char __user *buf, size_t size, loff_t *ppos)
{
    unsigned long p = *ppos;
    unsigned int count = size;
    int ret = 0;

    struct globalmem_dev *dev = filp->private_data; 

    if(p >= GLOBALMEM_SIZE) {
        return 0;
    }
    if(count > GLOBALMEM_SIZE - p){
        count = GLOBALMEM_SIZE - p;
    }

    // 除了这里，其他地方可以说和读函数一模一样
    if(copy_from_user(dev->mem + p, buf, count)) {
        ret = -EFAULT;
    } else {
        *ppos += count; 
        ret = count;

        printk(KERN_INFO "write %u bytes from %lu\n", count, p);
    }

    return ret;
}

//seek 修改文件偏移SEEK_SET:0, SEEK_CUR:1, SEEK_END;2
static loff_t globalmem_llseek(struct file *filp, loff_t offset, int orig)
{
    loff_t ret = 0;
    switch(orig) 
    {
        // 从文件开头开始偏移
    case 0: if(offset < 0) {
            ret = -EINVAL;
            break;
        }
        if((unsigned int)offset > GLOBALMEM_SIZE) {
            ret = -EINVAL;
            break;
        }    
        filp->f_pos = (unsigned int)offset;
        ret = filp->f_pos;  //filp的f_pos存放着当前文件的偏移
        break;
    
    // 从文件当前位置开始偏移
    case 1:
        if((filp->f_pos + offset) > GLOBALMEM_SIZE) {
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
static long globalmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct globalmem_dev *dev = filp->private_data;

    switch(cmd) 
    {
    case MEM_CLEAR:
        memset(dev->mem, 0, GLOBALMEM_SIZE);
        printk(KERN_INFO "globalmem is set to zero\n");
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

// 文件操作结构体
static const struct file_operations globalmem_fops = {
    .owner = THIS_MODULE,
    .llseek = globalmem_llseek,
    .read = globalmem_read,
    .write = globalmem_write,
    .unlocked_ioctl = globalmem_ioctl,
    .open = globalmem_open,
};


// 字符设备初始化
static void globalmem_setup_cdev(struct globalmem_dev *dev, int index)
{
    int err;
    int devno = MKDEV(globalmen_major, index);  // 主设备号固定，次设备号参数，生成一个设备号

    cdev_init(&dev->cdev, &globalmem_fops);     // 初始化字符设备结构体
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);       // 注册字符设备
    if(err) {
        printk(KERN_NOTICE "Error %d adding globalmem%d",err, index);
    }
}

// 模块加载初始化函数
static int __init globalmem_init(void)
{
    int ret;
    dev_t devno = MKDEV(globalmen_major, 0);    // 生产一个次设备号为0的设备号

    // 如果存在主设备号，则注册该设备号的设备；否则就动态分配一个
    if(globalmen_major) {
        ret = register_chrdev_region(devno, 1, "globalmem");
    } else {
        ret = alloc_chrdev_region(&devno, 0, 1, "globalmem");
        globalmen_major = MAJOR(devno);
    }

    if(ret < 0) {
        return ret;
    }

    // 申请一个空间用于存放设备的特定信息
    globalmem_devp = kzalloc(sizeof(struct globalmem_dev), GFP_KERNEL);
    if(!globalmem_devp) {
        ret = -ENOMEM;
        goto fail_malloc;
    }

    globalmem_setup_cdev(globalmem_devp, 0);
    return 0;

    fail_malloc:
    unregister_chrdev_region(devno, 1);
    return ret;
}
module_init(globalmem_init);

// 卸载函数
static void __exit globalmem_exit(void)
{
    cdev_del(&globalmem_devp->cdev);
    kfree(globalmem_devp);
    unregister_chrdev_region(MKDEV(globalmen_major, 0), 1);
}
module_exit(globalmem_exit);

MODULE_AUTHOR("Barry Song <baohua@kernel.org");
MODULE_LICENSE("GPL v2");
