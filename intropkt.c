#define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/udp.h>
#include <linux/virtio.h>
#include <net/sch_generic.h>

// #include "intropkt_virtnet.h"

int t = 100; /* Time interval in ms */
int p; /* Probability threshold */

char* ifname = "eth0";
__u32 src_ip_addr = 0x0103a8c0;
__u32 dst_ip_addr = 0x0102a8c0;
__u64 src_mac_addr = 0xf0bf97697d74;
__u64 dst_mac_addr = 0x180373b2bb5b;
__u16 src_port = 123;
__u16 dst_port = 1234;

static struct net_device *intropkt_find_net_device(char *ifname) {
	struct net_device *dev = first_net_device(&init_net);
	while (dev) {
	    printk(KERN_INFO "[%s]\n", dev->name);
	    if (!strcmp(ifname, dev->name))
	    	break;
	    dev = next_net_device(dev);
	}

	if (dev) {
		printk(KERN_INFO "found the dev %s\n", dev->name);
	}
	else {
		printk(KERN_INFO "cannot find the dev %s\n", ifname);
	}

	return dev;
}

static struct sk_buff *intropkt_alloc_skb(struct net_device *dev, int pktlen) {
	unsigned int extralen = LL_RESERVED_SPACE(dev);
	struct sk_buff *skb = NULL;
	unsigned int size;

	size = pktlen + 64 + extralen;

	skb = __netdev_alloc_skb(dev, size, GFP_NOWAIT);

	if (likely(skb))
		skb_reserve(skb, extralen - 16);

	return skb;
}

static void intropkt_fill_skb_data(struct sk_buff *skb, char *data, int datalen) {
	char *pktdata;
	pktdata = (char *) skb_put(skb, datalen);

	strcpy(pktdata, data);
}

static struct sk_buff *intropkt_create_skb(struct net_device *dev, char* data) {
	struct sk_buff *skb = NULL;
	__u8 *eth;
	struct udphdr *udph;
	int pktlen, datalen, iplen;
	struct iphdr *iph;
	__be16 protocol = htons(ETH_P_IP);
	__be32 *mpls;

	datalen = strlen(data) + 1; //??
	pktlen = datalen + 14 + 20 +8;

	skb = intropkt_alloc_skb(dev, pktlen);
	if (!skb) {
		printk(KERN_INFO "No memory\n");
		return NULL;
	}

	prefetchw(skb->data);
	skb_reserve(skb, 16);

	/*  Reserve for ethernet and IP header  */
	eth = (__u8 *) skb_push(skb, 14);
	mpls = (__be32 *)skb_put(skb, 0);

	skb_reset_mac_header(skb);
	skb_set_network_header(skb, skb->len);
	iph = (struct iphdr *) skb_put(skb, sizeof(struct iphdr));

	skb_set_transport_header(skb, skb->len);
	udph = (struct udphdr *) skb_put(skb, sizeof(struct udphdr));
	skb->priority = 0;

	__u8 hh[14];

	hh[0] = dst_mac_addr >> 40;
	hh[1] = dst_mac_addr >> 32;
	hh[2] = dst_mac_addr >> 24;
	hh[3] = dst_mac_addr >> 16;
	hh[4] = dst_mac_addr >> 8;
	hh[5] = dst_mac_addr;
	hh[6] = src_mac_addr >> 40;
	hh[7] = src_mac_addr >> 32;
	hh[8] = src_mac_addr >> 24;
	hh[9] = src_mac_addr >> 16;
	hh[10] = src_mac_addr >> 8;
	hh[11] = src_mac_addr;
	hh[12] = 0x00;
	hh[13] = 0x00;

	memcpy(eth, hh, 12);

	*(__be16 *) & eth[12] = protocol;

	udph->source = htons(src_port);  /* source port  */
	udph->dest = htons(dst_port); /* dest port */
	udph->len = htons(datalen + 8);	/* DATA + udphdr */
	udph->check = 0;

	iph->ihl = 5;
	iph->version = 4;
	iph->ttl = 32;
	iph->tos = 0;
	iph->protocol = IPPROTO_UDP;	/* UDP */
	iph->saddr = src_ip_addr; /* source ip */
	iph->daddr = dst_ip_addr; /* dest ip */
	iph->id = htons(0);

	iph->frag_off = 0;
	iplen = 20 + 8 + datalen;
	iph->tot_len = htons(iplen);
	ip_send_check(iph);
	skb->protocol = protocol;
	skb->dev = dev;
	skb->pkt_type = PACKET_HOST;

	skb->ip_summed = CHECKSUM_NONE;

	intropkt_fill_skb_data(skb, data, datalen);

	return skb;
}


static int pfifo_tail_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	if (likely(skb_queue_len(&sch->q) < qdisc_dev(sch)->tx_queue_len))
		return qdisc_enqueue_tail(skb, sch);

	/* queue full, remove one skb to fulfill the limit */
	__qdisc_queue_drop_head(sch, &sch->q);
	sch->qstats.drops++;
	qdisc_enqueue_tail(skb, sch);

	return NET_XMIT_CN;
}

static inline int qdisc_head_enqueue(struct sk_buff *skb, struct Qdisc *sch) {
	// __skb_queue_tail(&sch->q, skb);
	// sch->qstats.backlog += qdisc_pkt_len(skb);

	// return NET_XMIT_SUCCESS;
	printk(KERN_INFO "q len %d\n",
		skb_queue_len(&sch->q));

	int ret = sch->enqueue(skb, sch);
	// return pfifo_tail_enqueue(skb, sch);
	printk(KERN_INFO "q len %d\n",
		skb_queue_len(&sch->q));

	return ret;
}

static inline int qdisc_restart(struct Qdisc *q)
{
	struct net_device *dev;
	struct sk_buff *skb;

	dev = qdisc_dev(q);
	/* Dequeue packet */
	skb = q->dequeue(q);
	if (unlikely(!skb))
		return 0;
	

	return dev->netdev_ops->ndo_start_xmit(skb, dev);
}

static inline int __dev_xmit_skb(struct sk_buff *skb, struct Qdisc *q)
{
	spinlock_t *root_lock = qdisc_lock(q);
	bool contended;
	int rc;

	qdisc_skb_cb(skb)->pkt_len = skb->len;
	qdisc_calculate_pkt_len(skb, q);
	contended = qdisc_is_running(q);
	if (unlikely(contended))
		spin_lock(&q->busylock);

	spin_lock(root_lock);

	skb_dst_force(skb);
	rc = qdisc_head_enqueue(skb, q) & NET_XMIT_MASK;
	if (qdisc_run_begin(q)) {
		if (unlikely(contended)) {
			spin_unlock(&q->busylock);
			contended = false;
		}
		//__qdisc_run(q);
		int quota = 64;

		while (qdisc_restart(q)) {
			if (--quota <= 0 || need_resched()) {
				__netif_schedule(q);
				break;
			}
		}

		qdisc_run_end(q);
	}
	spin_unlock(root_lock);
	if (unlikely(contended))
		spin_unlock(&q->busylock);
	return rc;
}

int init_module(void)
{
	printk(KERN_INFO "insmod intropkt\n");

	struct net_device *dev;
	struct netdev_queue *txq;
	struct Qdisc *q;

	dev = intropkt_find_net_device(ifname);
	txq = netdev_get_tx_queue(dev, 0);
	q = txq->qdisc;

	printk(KERN_INFO "Test %d\n", dev->real_num_tx_queues);

	struct sk_buff *skb = NULL;

	char *testdata = "this is a test data. hahahahahah";

	skb = intropkt_create_skb(dev, testdata);

	// int ret = dev->netdev_ops->ndo_start_xmit(skb, dev);
	int ret = __dev_xmit_skb(skb, q);
	ret = __dev_xmit_skb(skb, q);
	ret = __dev_xmit_skb(skb, q);
	ret = __dev_xmit_skb(skb, q);
	ret = __dev_xmit_skb(skb, q);

	// int ret = dev_queue_xmit(skb);


	switch (ret) {
	case NET_XMIT_SUCCESS:
		printk(KERN_INFO "packet is sent\n");
		break;
	case NET_XMIT_DROP:
	case NET_XMIT_CN:
	case NETDEV_TX_BUSY:
	default:
		printk(KERN_INFO "packet is not sent\n");
		break;
	}

	return 0;
}

void cleanup_module()
{
	printk(KERN_INFO "rmmod intropkt\n");
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hoang");
MODULE_DESCRIPTION("A kernel module for sending introspection packets");
