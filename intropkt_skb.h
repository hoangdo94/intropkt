#include <linux/skbuff.h>
#include <net/udp.h>

#define SRC_IP_ADDR 	0x0103a8c0
#define DST_IP_ADDR 	0x0102a8c0
#define SRC_MAC_ADDR 	0xf0bf97697d74
#define DST_MAC_ADDR 	0x180373b2bb5b
#define SRC_PORT 			3000
#define DST_PORT 			1234

#define PAYLOAD 			"PACKET PAYLOAD"

static inline struct sk_buff *intropkt_alloc_skb(struct net_device *dev, int pktlen) {
	unsigned int extralen = LL_RESERVED_SPACE(dev);
	struct sk_buff *skb = NULL;
	unsigned int size;

	size = pktlen + 64 + extralen;

	skb = __netdev_alloc_skb(dev, size, GFP_NOWAIT);

	if (likely(skb))
		skb_reserve(skb, extralen - 16);

	return skb;
}

static inline void intropkt_fill_skb_data(struct sk_buff *skb, char *payload, int datalen) {
	char *pktdata;
	pktdata = (char *) skb_put(skb, datalen);

	strcpy(pktdata, payload);
}

static inline struct sk_buff *intropkt_create_skb(struct net_device *dev, char* payload) {
	struct sk_buff *skb = NULL;
	__u8 *eth;
	struct udphdr *udph;
	int pktlen, datalen, iplen;
	struct iphdr *iph;
	__be16 protocol = htons(ETH_P_IP);
	__be32 *mpls;

	datalen = strlen(payload) + 1; //??
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

  __u64 src_mac_addr = SRC_MAC_ADDR;
  __u64 dst_mac_addr = DST_MAC_ADDR;

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

	udph->source = htons(SRC_PORT);  /* source port  */
	udph->dest = htons(DST_PORT); /* dest port */
	udph->len = htons(datalen + 8);	/* payload + udphdr */
	udph->check = 0;

	iph->ihl = 5;
	iph->version = 4;
	iph->ttl = 32;
	iph->tos = 0;
	iph->protocol = IPPROTO_UDP;	/* UDP */
	iph->saddr = SRC_IP_ADDR; /* source ip */
	iph->daddr = DST_IP_ADDR; /* dest ip */
	iph->id = htons(0);

	iph->frag_off = 0;
	iplen = 20 + 8 + datalen;
	iph->tot_len = htons(iplen);
	ip_send_check(iph);
	skb->protocol = protocol;
	skb->dev = dev;
	skb->pkt_type = PACKET_HOST;

	skb->ip_summed = CHECKSUM_NONE;

	intropkt_fill_skb_data(skb, payload, datalen);

	return skb;
}
