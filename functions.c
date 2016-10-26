#include "intropkt.h"

uint64_t get_tdh(void) {
	return 0;
}

void set_tdh(uint64_t addr) {
	printk(KERN_INFO "set TDH to %llu\n", addr);
}

