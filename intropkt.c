#define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/netdevice.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/random.h>

#include "intropkt_skb.h"
#include "intropkt_qdisc.h"

#define IFNAME 	"eth0" 	/* Interface */
#define T 			1000L 		/* Time interval in ms */
#define P 			50 		/* Threshold (0 - 255)*/

#define MS_TO_NS(x)	(x * 1E6L)

static struct net_device *dev;
static struct Qdisc *q;
static struct hrtimer hr_timer;
static ktime_t ktime;


static struct net_device *intropkt_find_net_device(char *ifname) {
	struct net_device *dev = first_net_device(&init_net);
	while (dev) {
	    pr_debug("[%s]\n", dev->name);
	    if (!strcmp(ifname, dev->name))
	    	break;
	    dev = next_net_device(dev);
	}

	if (dev) {
		pr_debug("found the dev %s\n", dev->name);
	}
	else {
		pr_debug("cannot find the dev %s\n", ifname);
	}

	return dev;
}

static int send_packet(void) {
	struct sk_buff *skb = NULL;
	skb = intropkt_create_skb(dev, PAYLOAD);
	int ret = head_xmit_skb(skb, q);

	switch (ret) {
	case NET_XMIT_SUCCESS:
		pr_debug("packet is sent\n");
		break;
	case NET_XMIT_DROP:
	case NET_XMIT_CN:
	case NETDEV_TX_BUSY:
	default:
		pr_debug("packet is not sent\n");
		break;
	}
	return ret;
}

enum hrtimer_restart send_packet_hrtimer_callback( struct hrtimer *timer )
{
	__u8 rd;
	get_random_bytes(&rd, sizeof rd);

	if (likely(rd >= P)) {
		pr_debug("%d >= %d, attempt to send\n", rd, P);
		send_packet();
	} else {
		pr_debug("%d < %d, not send\n", rd, P);
	}


	hrtimer_start( &hr_timer, ktime, HRTIMER_MODE_REL );
	return HRTIMER_NORESTART;
}

int init_module(void)
{
	printk(KERN_INFO "insmod intropkt\n");

	dev = intropkt_find_net_device(IFNAME);
	if (likely(!dev))
		return -1;

	q = netdev_get_tx_queue(dev, 0)->qdisc;

	pr_debug("HR Timer module installing\n");

	ktime = ktime_set( 0, MS_TO_NS(T) );

	hrtimer_init( &hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );

	hr_timer.function = &send_packet_hrtimer_callback;

	pr_debug( "Starting timer to fire in %ldms (%ld)\n", T, jiffies );

	hrtimer_start( &hr_timer, ktime, HRTIMER_MODE_REL );

	return 0;
}

void cleanup_module()
{
	printk(KERN_INFO "rmmod intropkt\n");
  hrtimer_cancel(&hr_timer);
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hoang");
MODULE_DESCRIPTION("A kernel module for sending introspection packets");
