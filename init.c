#include "intropkt.h"

int init_module(void)
{
	printk(KERN_INFO "insmod intropkt\n");
	get_tdh();
	set_tdh(0x1234);
	return 0;
}

