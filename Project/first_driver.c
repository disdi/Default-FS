#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>

static int __init mfd_init(void) /* Constructor */
{
	printk(KERN_INFO "mfd registered");
	return 0;
}

static void __exit mfd_exit(void) /* Destructor */
{
	printk(KERN_INFO "mfd unregistered");
}

module_init(mfd_init);
module_exit(mfd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anil Kumar Pugalia <email@sarika-pugs.com>");
MODULE_DESCRIPTION("My First Driver");
