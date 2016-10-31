#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/scatterlist.h>
#include <linux/if_vlan.h>
#include <linux/slab.h>

struct virtnet_stats {
	struct u64_stats_sync syncp;
	u64 tx_bytes;
	u64 tx_packets;

	u64 rx_bytes;
	u64 rx_packets;
};

struct virtnet_info {
	struct virtio_device *vdev;
	struct virtqueue *rvq, *svq, *cvq;
	struct net_device *dev;
	struct napi_struct napi;
	unsigned int status;

	/* Number of input buffers, and max we've ever had. */
	unsigned int num, max;

	/* I like... big packets and I cannot lie! */
	bool big_packets;

	/* Host will merge rx buffers for big packets (shake it! shake it!) */
	bool mergeable_rx_bufs;

	/* Active statistics */
	struct virtnet_stats __percpu *stats;

	/* Work struct for refilling if we run low on memory. */
	struct delayed_work refill;

	/* Chain pages by the private ptr. */
	struct page *pages;

	/* fragments + linear part + virtio header */
	struct scatterlist rx_sg[MAX_SKB_FRAGS + 2];
	struct scatterlist tx_sg[MAX_SKB_FRAGS + 2];
};

struct skb_vnet_hdr {
	union {
		struct virtio_net_hdr hdr;
		struct virtio_net_hdr_mrg_rxbuf mhdr;
	};
	unsigned int num_sg;
};

struct padded_vnet_hdr {
	struct virtio_net_hdr hdr;
	/*
	 * virtio_net_hdr should be in a separated sg buffer because of a
	 * QEMU bug, and data sg buffer shares same page with this header sg.
	 * This padding makes next sg 16 byte aligned after virtio_net_hdr.
	 */
	char padding[6];
};

static inline struct skb_vnet_hdr *skb_vnet_hdr(struct sk_buff *skb)
{
	return (struct skb_vnet_hdr *)skb->cb;
}

static unsigned int free_old_xmit_skbs(struct virtnet_info *vi)
{
	struct sk_buff *skb;
	unsigned int len, tot_sgs = 0;
	struct virtnet_stats __percpu *stats = this_cpu_ptr(vi->stats);

	while ((skb = virtqueue_get_buf(vi->svq, &len)) != NULL) {
		pr_debug("Sent skb %p\n", skb);

		u64_stats_update_begin(&stats->syncp);
		stats->tx_bytes += skb->len;
		stats->tx_packets++;
		u64_stats_update_end(&stats->syncp);

		tot_sgs += skb_vnet_hdr(skb)->num_sg;
		dev_kfree_skb_any(skb);
	}
	return tot_sgs;
}

static int xmit_skb(struct virtnet_info *vi, struct sk_buff *skb)
{
	struct skb_vnet_hdr *hdr = skb_vnet_hdr(skb);
	const unsigned char *dest = ((struct ethhdr *)skb->data)->h_dest;

	pr_debug("%s: xmit %p %pM\n", vi->dev->name, skb, dest);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		hdr->hdr.flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
		hdr->hdr.csum_start = skb_checksum_start_offset(skb);
		hdr->hdr.csum_offset = skb->csum_offset;
	} else {
		hdr->hdr.flags = 0;
		hdr->hdr.csum_offset = hdr->hdr.csum_start = 0;
	}

	if (skb_is_gso(skb)) {
		hdr->hdr.hdr_len = skb_headlen(skb);
		hdr->hdr.gso_size = skb_shinfo(skb)->gso_size;
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_TCPV6;
		else if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP)
			hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_UDP;
		else
			BUG();
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCP_ECN)
			hdr->hdr.gso_type |= VIRTIO_NET_HDR_GSO_ECN;
	} else {
		hdr->hdr.gso_type = VIRTIO_NET_HDR_GSO_NONE;
		hdr->hdr.gso_size = hdr->hdr.hdr_len = 0;
	}

	hdr->mhdr.num_buffers = 0;

	/* Encode metadata header at front. */
	if (vi->mergeable_rx_bufs)
		sg_set_buf(vi->tx_sg, &hdr->mhdr, sizeof hdr->mhdr);
	else
		sg_set_buf(vi->tx_sg, &hdr->hdr, sizeof hdr->hdr);

	hdr->num_sg = skb_to_sgvec(skb, vi->tx_sg + 1, 0, skb->len) + 1;
	return virtqueue_add_buf(vi->svq, vi->tx_sg, hdr->num_sg,
					0, skb);
}

static netdev_tx_t start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct virtnet_info *vi = netdev_priv(dev);
	int capacity;

	/* Free up any pending old buffers before queueing new ones. */
	free_old_xmit_skbs(vi);

	/* Try to transmit */
	capacity = xmit_skb(vi, skb);

	/* This can happen with OOM and indirect buffers. */
	if (unlikely(capacity < 0)) {
		if (net_ratelimit()) {
			if (likely(capacity == -ENOMEM)) {
				dev_warn(&dev->dev,
					 "TX queue failure: out of memory\n");
			} else {
				dev->stats.tx_fifo_errors++;
				dev_warn(&dev->dev,
					 "Unexpected TX queue failure: %d\n",
					 capacity);
			}
		}
		dev->stats.tx_dropped++;
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	virtqueue_kick(vi->svq);

	/* Don't wait up for transmitted skbs to be freed. */
	skb_orphan(skb);
	nf_reset(skb);

	/* Apparently nice girls don't return TX_BUSY; stop the queue
	 * before it gets out of hand.  Naturally, this wastes entries. */
	if (capacity < 2+MAX_SKB_FRAGS) {
		netif_stop_queue(dev);
		if (unlikely(!virtqueue_enable_cb_delayed(vi->svq))) {
			/* More just got used, free them then recheck. */
			capacity += free_old_xmit_skbs(vi);
			if (capacity >= 2+MAX_SKB_FRAGS) {
				netif_start_queue(dev);
				virtqueue_disable_cb(vi->svq);
			}
		}
	}

	return NETDEV_TX_OK;
}
