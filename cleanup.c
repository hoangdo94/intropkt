#include "intropkt.h"

void cleanup_module()
{
	printk(KERN_INFO "rmmod intropkt\n");
}

