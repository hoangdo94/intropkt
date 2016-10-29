#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
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
