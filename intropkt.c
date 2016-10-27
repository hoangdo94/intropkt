#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/udp.h>

char* ifname = "eth1";
struct net_device *odev;

int intropkt_find_net_device(void);

static struct sk_buff *intropkt_alloc_skb(struct net_device *dev) {
	unsigned int extralen = LL_RESERVED_SPACE(dev);
	struct sk_buff *skb = NULL;
	unsigned int size;

	size = 64 + 64 + extralen;

	skb = __netdev_alloc_skb(dev, size, GFP_NOWAIT);

	if (likely(skb))
		skb_reserve(skb, extralen - 16);

	return skb;
}

// static struct sk_buff *intropkt_fill_skb(struct net_device *dev) {
// 	struct sk_buff *skb = NULL;
// 	__u8 *eth;
// 	struct udphdr *udph;
// 	int datalen, iplen;
// 	struct iphdr *iph;
// 	__be16 protocol = htons(ETH_P_IP);
// 	__be32 *mpls;
// 	__be16 *vlan_tci = NULL;                 /* Encapsulates priority and VLAN ID */
// 	__be16 *vlan_encapsulated_proto = NULL;  /* packet type ID field (or len) for VLAN tag */
// 	__be16 *svlan_tci = NULL;                /* Encapsulates priority and SVLAN ID */
// 	__be16 *svlan_encapsulated_proto = NULL; /* packet type ID field (or len) for SVLAN tag */
// 	u16 queue_map;


// 	skb = pktgen_alloc_skb(dev);
// 	if (!skb) {
// 		printk(KERN_INFO "No memory\n");
// 		return NULL;
// 	}

// 	prefetchw(skb->data);
// 	skb_reserve(skb, 16);

// 	/*  Reserve for ethernet and IP header  */
// 	eth = (__u8 *) skb_push(skb, 14);
// 	mpls = (__be32 *)skb_put(skb, pkt_dev->nr_labels*sizeof(__u32));

// 	skb_reset_mac_header(skb);
// 	skb_set_network_header(skb, skb->len);
// 	iph = (struct iphdr *) skb_put(skb, sizeof(struct iphdr));

// 	skb_set_transport_header(skb, skb->len);
// 	udph = (struct udphdr *) skb_put(skb, sizeof(struct udphdr));
// 	skb_set_queue_mapping(skb, queue_map);
// 	skb->priority = pkt_dev->skb_priority;

// 	memcpy(eth, pkt_dev->hh, 12);
// 	*(__be16 *) & eth[12] = protocol;

// 	/* Eth + IPh + UDPh + mpls */
// 	datalen = 64 - 14 - 20 - 8;

// 	udph->source = htons(80);
// 	udph->dest = htons(80);
// 	udph->len = htons(datalen + 8);	/* DATA + udphdr */
// 	udph->check = 0;

// 	iph->ihl = 5;
// 	iph->version = 4;
// 	iph->ttl = 32;
// 	iph->tos = 0;
// 	iph->protocol = IPPROTO_UDP;	/* UDP */
// 	iph->saddr = pkt_dev->cur_saddr;
// 	iph->daddr = pkt_dev->cur_daddr;
// 	iph->id = htons(pkt_dev->ip_id);

// 	iph->frag_off = 0;
// 	iplen = 20 + 8 + datalen;
// 	iph->tot_len = htons(iplen);
// 	ip_send_check(iph);
// 	skb->protocol = protocol;
// 	skb->dev = dev;
// 	skb->pkt_type = PACKET_HOST;

// 	//intropkt_finalize_skb(pkt_dev, skb, datalen);

// 	skb->ip_summed = CHECKSUM_NONE;
	

// 	return skb;
// }

int intropkt_find_net_device(void) {
	odev = first_net_device(&init_net);
	while (odev) {
	    printk(KERN_INFO "found [%s]\n", odev->name);
	    if (!strcmp(ifname, odev->name))
	    	break;
	    odev = next_net_device(odev);
	}

	if (odev) {
		printk(KERN_INFO "found the dev %s\n", odev->name);
		return 0;
	}
	else {
		printk(KERN_INFO "cannot find the dev %s\n", ifname);
		return -1;
	}
}

int init_module(void)
{
	printk(KERN_INFO "insmod intropkt\n");
	intropkt_find_net_device();
	return 0;
}

void cleanup_module()
{
	printk(KERN_INFO "rmmod intropkt\n");
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hoang Do <nkhdo@smu.edu.sg>");
MODULE_DESCRIPTION("A kernel module for sending introspection packets");
