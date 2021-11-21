// 字符设备秒驱动

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define SECOND_MAJOR 248

static int second_major = SECOND_MAJOR;
module_param(second_major, int, S_IRUGO);

struct second_dev
{
    struct cdev cdev;
    atomic_t counter;
    struct timer_list s_timer;
};

static struct second_dev *second_devp;

static void second_timer_handler(unsigned long arg)
{
    mod_timer(&second_devp->s_timer, jiffies + HZ); // 触发下一秒计时
    atomic_inc(&second_devp->counter);      // 增加秒计数
}