#include <net/pkt_sched.h>
#include <net/sch_generic.h>

struct pfifo_fast_priv {
	u32 bitmap;
	struct sk_buff_head q[3];
};

static inline struct sk_buff_head *band2list(struct pfifo_fast_priv *priv,
					     int band)
{
	return priv->q + band;
}

static inline int __qdisc_enqueue_head(struct sk_buff *skb, struct Qdisc *sch,
				       struct sk_buff_head *list)
{
	__skb_queue_head(list, skb);
	sch->qstats.backlog += qdisc_pkt_len(skb);

	return NET_XMIT_SUCCESS;
}

static inline int pfifo_fast_enqueue_head(struct sk_buff *skb, struct Qdisc *qdisc)
{
	if (skb_queue_len(&qdisc->q) < qdisc_dev(qdisc)->tx_queue_len) {
		int band = 0;
		struct pfifo_fast_priv *priv = qdisc_priv(qdisc);
		struct sk_buff_head *list = band2list(priv, band);

		priv->bitmap |= (1 << band);
		qdisc->q.qlen++;
		return __qdisc_enqueue_head(skb, qdisc, list);
	}

	return qdisc_drop(skb, qdisc);
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

static inline int head_xmit_skb(struct sk_buff *skb, struct Qdisc *q)
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
	rc = pfifo_fast_enqueue_head(skb, q) & NET_XMIT_MASK;
	if (qdisc_run_begin(q)) {
		if (unlikely(contended)) {
			spin_unlock(&q->busylock);
			contended = false;
		}
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
