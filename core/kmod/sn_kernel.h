/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2014 Sangjin Han All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/* Dual BSD/GPL */

#ifndef _SN_KERNEL_H

#ifdef __KERNEL__

#include <linux/netdevice.h>
#include <linux/miscdevice.h>

#define MODULE_NAME "bess"

#define log_info(fmt, ...)                                                     \
	printk(KERN_INFO "%s - %s():%d " pr_fmt(fmt), MODULE_NAME, __func__,   \
	       __LINE__, ##__VA_ARGS__)

#define log_err(fmt, ...)                                                      \
	printk(KERN_ERR "%s - %s():%d " pr_fmt(fmt), MODULE_NAME, __func__,    \
	       __LINE__, ##__VA_ARGS__)

#define MAX_QUEUES 128

#define MAX_BATCH 32

DECLARE_PER_CPU(int, in_batched_polling);

struct sn_device;

#define SN_NET_XMIT_BUFFERED -1

struct sn_queue {
	struct sn_device *dev;
	int queue_id;

	struct llring *drv_to_sn;
	struct llring *sn_to_drv;

	union {
		struct {
			struct sn_queue_tx_stats {
				u64 packets;
				u64 bytes;
				u64 dropped;
				u64 throttled;
				u64 descriptor;
			} stats;

			struct netdev_queue *netdev_txq;

			struct tx_queue_opts opts;
		} tx;

		struct {
			struct sn_queue_rx_stats {
				u64 packets;
				u64 bytes;
				u64 dropped;
				u64 polls;
				u64 interrupts;
				u64 ll_polls;
			} stats;

			struct sn_rxq_registers *rx_regs;
			struct napi_struct napi;

			spinlock_t lock; /* kernel has its own locks for TX */

			struct rx_queue_opts opts;
		} rx;
	};
} ____cacheline_aligned_in_smp;

struct sn_ops {
	/* Returns NET_XMIT_SUCCESS, NET_XMIT_CN, or NET_XMIT_DROP.
	 * The caller sets tx_meta, and the callee is responsible to
	 * transmit it along with the packet data. */
	int (*do_tx)(struct sn_queue *tx_queue, struct sk_buff *skb,
		     struct sn_tx_metadata *tx_meta);

	/* Receives a packet and returns an skb (NULL if no pending packet).
	 * The callee fills the given rx_meta, then the caller will take care
	 * of it (except for packet length) */
	struct sk_buff *(*do_rx)(struct sn_queue *rx_queue,
				 struct sn_rx_metadata *rx_meta);

	/* Returns # of packets received */
	int (*do_rx_batch)(struct sn_queue *rx_queue,
			   struct sn_rx_metadata *rx_meta, struct sk_buff **skb,
			   int max_cnt);

	/* Returns true if there are pending RX packets */
	bool (*pending_rx)(struct sn_queue *rx_queue);

	void (*flush_tx)(void);
};

struct sn_device {
	struct net_device *netdev;

	int num_txq;
	int num_rxq;

	struct sn_queue *tx_queues[MAX_QUEUES];
	struct sn_queue *rx_queues[MAX_QUEUES];

	/* cpu -> txq mapping */
	int cpu_to_txq[NR_CPUS];

	/* cpu -> rxq array terminating with -1 */
	int cpu_to_rxqs[NR_CPUS][MAX_QUEUES + 1];

	struct sn_ops *ops;
};

/* function prototypes defined in sn_netdev.c */
int sn_create_netdev(void *bar, struct sn_device **dev_ret);
int sn_register_netdev(void *bar, struct sn_device *dev);
void sn_release_netdev(struct sn_device *dev);
void sn_trigger_softirq(void *info); /* info is (struct sn_device *) */
void sn_trigger_softirq_with_qid(void *info, int rxq);

#endif

#endif
