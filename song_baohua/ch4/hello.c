// 一个简单的Linux内核模块

#include <linux/init.h>
#include <linux/module.h>

static int __init hello_init(void)
{
    printk(KERN_INFO "hello world enter\n");
    return 0;
}

module_init(hello_init);

static void __exit hello_exit(void)
{
    printk(KERN_INFO "hello world exit\n");
}

module_exit(hello_exit);

MODULE_AUTHOR("Barry Song<dda@daf.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("A simple Hello World Module");
MODULE_ALIAS("a simple module");
