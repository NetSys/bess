// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <linux/version.h>

#include "sn_common.h"
#include "sn_kernel.h"
#include "../snbuf_layout.h"

static void
sn_dump_queue_mapping(struct sn_device *dev) __attribute__((unused));

#define BUFS_PER_CPU 32
struct snb_cache {
	phys_addr_t paddr[BUFS_PER_CPU];
	int cnt;
};

DEFINE_PER_CPU(struct snb_cache, snb_cache);

#define MAX_TX_BUFFER_QUEUE_CNT	4

struct sn_tx_buffer {
	int tx_queue_cnt;

	struct sn_tx_buffer_queue {
		struct sn_queue *queue;
		struct sk_buff *skb_arr[MAX_BATCH];
		struct sn_tx_metadata meta_arr[MAX_BATCH];
		int cnt;
	} queue_arr[MAX_TX_BUFFER_QUEUE_CNT];
};

DEFINE_PER_CPU(struct sn_tx_buffer, tx_buffer);

/* User applications are expected to open /dev/bess every time
 * they create a network device */
static int sn_host_open(struct inode *inode, struct file *filp)
{
	filp->f_flags |= O_CLOEXEC;
	filp->private_data = NULL;

	return 0;
}

static int sn_host_release(struct inode *inode, struct file *filp)
{
	if (filp->private_data) {
		sn_release_netdev((struct sn_device *) filp->private_data);
		filp->private_data = NULL;
	}

	return 0;
}

static int load_from_cache(phys_addr_t paddr[], int cnt)
{
	struct snb_cache *cache;
	int loaded;

	cache = this_cpu_ptr(&snb_cache);

	loaded = min(cnt, cache->cnt);

	memcpy(paddr, &cache->paddr[cache->cnt - loaded],
			loaded * sizeof(phys_addr_t));
	cache->cnt -= loaded;

	return loaded;
}

static int store_to_cache(phys_addr_t paddr[], int cnt)
{
	struct snb_cache *cache;
	int free_slots;
	int stored;

	cache = this_cpu_ptr(&snb_cache);

	free_slots = BUFS_PER_CPU - cache->cnt;
	stored = min(cnt, free_slots);

	memcpy(&cache->paddr[cache->cnt], paddr, stored * sizeof(phys_addr_t));
	cache->cnt += stored;

	return stored;
}

static int alloc_snb_burst(struct sn_queue *queue, phys_addr_t paddr[], int cnt)
{
	int loaded;

	loaded = load_from_cache(paddr, cnt);
	if (loaded == cnt)
		return cnt;

	cnt = llring_sc_dequeue_burst(queue->sn_to_drv, &paddr[loaded],
			cnt - loaded);
	return loaded + cnt;
}

static void free_snb_bulk(struct sn_queue *queue, phys_addr_t paddr[], int cnt)
{
	int stored;
	int ret;

	stored = store_to_cache(paddr, cnt);
	if (stored == cnt)
		return;

	ret = llring_sp_enqueue_bulk(queue->drv_to_sn,
			&paddr[stored], cnt - stored);

	if (ret == -LLRING_ERR_NOBUF && net_ratelimit()) {
		log_err("%s: RX free queue overflow!\n",
				queue->dev->netdev->name);
	}
}

static int sn_host_do_tx_batch(struct sn_queue *queue,
		struct sk_buff *skb_arr[],
		struct sn_tx_metadata meta_arr[],
		int cnt_requested)
{
	int cnt_to_send;
	int cnt;
	int ret;
	int i;

	phys_addr_t paddr_arr[MAX_BATCH];

	cnt_to_send = min(cnt_requested,
			(int)llring_free_count(queue->drv_to_sn));
	cnt_to_send = min(cnt_to_send, MAX_BATCH);

	cnt = alloc_snb_burst(queue, paddr_arr, cnt_to_send);
	queue->tx.stats.descriptor += cnt_requested - cnt;

	if (cnt == 0)
		return 0;

	for (i = 0; i < cnt; i++) {
		struct sk_buff *skb = skb_arr[i];
		phys_addr_t paddr = paddr_arr[i];
		struct sn_tx_desc *tx_desc;
		char *dst_addr;

		int j;

		dst_addr = phys_to_virt(paddr + SNBUF_DATA_OFF);
		tx_desc = phys_to_virt(paddr + SNBUF_SCRATCHPAD_OFF);

		tx_desc->total_len = skb->len;
		tx_desc->meta = meta_arr[i];

		memcpy(dst_addr, skb->data, skb_headlen(skb));
		dst_addr += skb_headlen(skb);

		for (j = 0; j < skb_shinfo(skb)->nr_frags; j++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[j];

			memcpy(dst_addr, skb_frag_address(frag),
					skb_frag_size(frag));
			dst_addr += skb_frag_size(frag);
		}
	}

	ret = llring_sp_enqueue_burst(queue->drv_to_sn, paddr_arr, cnt);
	if (ret < cnt && net_ratelimit()) {
		/* It should never happen since we cap cnt with llring_count().
		 * If it does, snbufs leak. Ouch. */
		log_err("%s: queue %d is overflowing!\n",
				queue->dev->netdev->name, queue->queue_id);
	}

	return ret;
}

static void sn_host_flush_tx(void)
{
	struct sn_tx_buffer *buf;
	int i;
	int cpu = raw_smp_processor_id();

	buf = this_cpu_ptr(&tx_buffer);

	for (i = 0; i < buf->tx_queue_cnt; i++) {
		struct sn_tx_buffer_queue *buf_queue;
		struct sn_queue *queue;
		struct netdev_queue *netdev_txq;

		int lock_required;

		int sent;
		int j;

		buf_queue = &buf->queue_arr[i];
		queue = buf_queue->queue;
		netdev_txq = queue->tx.netdev_txq;

		lock_required = (netdev_txq->xmit_lock_owner != cpu);

		if (lock_required)
			HARD_TX_LOCK(queue->dev->netdev, netdev_txq, cpu);

		sent = sn_host_do_tx_batch(queue,
				buf_queue->skb_arr,
				buf_queue->meta_arr,
				buf_queue->cnt);

		if (lock_required)
			HARD_TX_UNLOCK(queue->dev->netdev, netdev_txq);

		queue->tx.stats.packets += sent;
		queue->tx.stats.dropped += buf_queue->cnt - sent;

		for (j = 0; j < buf_queue->cnt; j++) {
			struct sk_buff *skb = buf_queue->skb_arr[j];

			if (j < sent)
				queue->tx.stats.bytes += skb->len;

			dev_kfree_skb(skb);
		}
	}

	buf->tx_queue_cnt = 0;
}

static void sn_host_buffer_tx(struct sn_queue *queue, struct sk_buff *skb,
			 struct sn_tx_metadata *tx_meta)
{
	struct sn_tx_buffer *buf;
	struct sn_tx_buffer_queue *buf_queue;
	int i;

	buf = this_cpu_ptr(&tx_buffer);

again:
	buf_queue = NULL;

	for (i = 0; i < buf->tx_queue_cnt; i++)
		if (buf->queue_arr[i].queue == queue)
			buf_queue = &buf->queue_arr[i];

	if (!buf_queue) {
		if (buf->tx_queue_cnt == MAX_TX_BUFFER_QUEUE_CNT) {
			sn_host_flush_tx();
			goto again;
		}

		buf_queue = &buf->queue_arr[buf->tx_queue_cnt++];

		buf_queue->cnt = 0;
		buf_queue->queue = queue;
	}

	buf_queue->skb_arr[buf_queue->cnt] = skb;
	buf_queue->meta_arr[buf_queue->cnt] = *tx_meta;
	buf_queue->cnt++;

	if (buf_queue->cnt == MAX_BATCH)
		sn_host_flush_tx();
}

static int sn_host_do_tx(struct sn_queue *queue, struct sk_buff *skb,
			 struct sn_tx_metadata *tx_meta)
{
	int *polling;

	int ret;

	polling = this_cpu_ptr(&in_batched_polling);

	if (*polling) {
		sn_host_buffer_tx(queue, skb, tx_meta);
		return SN_NET_XMIT_BUFFERED;
	}

	ret = sn_host_do_tx_batch(queue, &skb, tx_meta, 1);
	return (ret == 1) ? NET_XMIT_SUCCESS : NET_XMIT_DROP;
}

static int sn_host_do_rx_batch(struct sn_queue *queue,
			       struct sn_rx_metadata *rx_meta,
			       struct sk_buff **skbs,
			       int max_cnt)
{
	phys_addr_t paddr[MAX_BATCH];

	int cnt;
	int i;

	max_cnt = min(max_cnt, MAX_BATCH);

	cnt = llring_sc_dequeue_burst(queue->sn_to_drv, paddr, max_cnt);
	if (cnt == 0)
		return 0;

	for (i = 0; i < cnt; i++) {
		struct sk_buff *skb;
		struct sn_rx_desc *rx_desc;

		int total_len;
		int copied;
		char *ptr;

		rx_desc = phys_to_virt(paddr[i] + SNBUF_SCRATCHPAD_OFF);
		if (!virt_addr_valid(rx_desc)) {
			log_err("invalid rx_desc %llx %p\n", paddr[i], rx_desc);
			continue;
		}

		rx_meta[i] = rx_desc->meta;
		total_len = rx_desc->total_len;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0))
		skb = skbs[i] = netdev_alloc_skb(queue->dev->netdev, total_len);
#else
		skb = skbs[i] = napi_alloc_skb(&queue->rx.napi, total_len);
#endif
		if (!skb) {
			if (net_ratelimit())
				log_err("skb alloc (%dB) failed\n", total_len);
			continue;
		}

		copied = 0;
		ptr = skb_put(skb, total_len);

		do {
			char *seg;
			uint16_t seg_len;

			seg = phys_to_virt(rx_desc->seg);
			seg_len = rx_desc->seg_len;

			memcpy(ptr + copied, phys_to_virt(rx_desc->seg),
					seg_len);

			copied += seg_len;
			rx_desc = phys_to_virt(rx_desc->next +
					SNBUF_SCRATCHPAD_OFF);
		} while (copied < total_len);
	}

	free_snb_bulk(queue, paddr, cnt);

	return cnt;
}

static bool sn_host_pending_rx(struct sn_queue *queue)
{
	return llring_count(queue->sn_to_drv) > 0;
}

static struct sn_ops sn_host_ops = {
	.do_tx 		= sn_host_do_tx,
	.do_rx_batch	= sn_host_do_rx_batch,
	.pending_rx 	= sn_host_pending_rx,
	.flush_tx	= sn_host_flush_tx,
};

static void sn_dump_queue_mapping(struct sn_device *dev)
{
	char buf[512];
	int buflen;

	int cpu;

	buflen = sprintf(buf, "CPU->TXQ mapping: ");

	for_each_online_cpu(cpu) {
		buflen += sprintf(buf + buflen, "%d->%d ", cpu,
				dev->cpu_to_txq[cpu]);
	}

	log_info("%s\n", buf);

	buflen = sprintf(buf, "CPU->RXQ mapping: ");

	for_each_online_cpu(cpu) {
		int i = 0;

		if (dev->cpu_to_rxqs[cpu][0] == -1)
			continue;

		if (dev->cpu_to_rxqs[cpu][1] == -1) {
			/* 1-to-1 mapping */
			buflen += sprintf(buf + buflen, "%d->%d ", cpu,
					dev->cpu_to_rxqs[cpu][0]);

		} else {
			buflen += sprintf(buf + buflen, "%d->[", cpu);

			while (dev->cpu_to_rxqs[cpu][i] != -1) {
				buflen += sprintf(buf + buflen, "%s%d",
						i > 0 ? ", " : "",
						dev->cpu_to_rxqs[cpu][i]);
				i++;
			}

			buflen += sprintf(buf + buflen, "] ");
		}
	}

	log_info("%s\n", buf);
}

static int sn_host_ioctl_create_netdev(phys_addr_t bar_phys,
				     struct sn_device **dev_ret)
{
	void *bar;
	int ret;

	bar = phys_to_virt(bar_phys);

	if (!virt_addr_valid(bar)) {
		log_err("invalid BAR address: phys=%llx virt=%p\n",
				bar_phys, bar);
		return -EFAULT;
	}

	ret = sn_create_netdev(bar, dev_ret);
	if (ret)
		return ret;

	(*dev_ret)->ops = &sn_host_ops;

	ret = sn_register_netdev(bar, *dev_ret);
	if (ret)
		*dev_ret = NULL;

	return ret;
}

static int sn_host_ioctl_kick_rx(struct sn_device *dev,
				 unsigned long cpumask)
{
	cpumask_var_t mask;
	int this_cpu;

	preempt_disable();

	/* smp_call_function_many does not consider the current CPU */
	this_cpu = smp_processor_id();
	if ((1 << this_cpu) & cpumask) {
		sn_trigger_softirq(dev);
		cpumask &= ~(1 << this_cpu);
	}

	if (!cpumask) {
		preempt_enable();
		return 0;
	}

	/* this should be fast unless CONFIG_CPUMASK_OFFSTACK is on */
	if (unlikely(!zalloc_cpumask_var(&mask, GFP_KERNEL))) {
		preempt_enable();
		return -ENOMEM;
	}

	*((unsigned long *) mask) = cpumask;

	smp_call_function_many(mask, sn_trigger_softirq, dev, 0);

	free_cpumask_var(mask);

	preempt_enable();

	return 0;
}

static int sn_host_ioctl_set_queue_mapping(
		struct sn_device *dev,
		struct sn_ioc_queue_mapping __user *map_user)
{
	struct sn_ioc_queue_mapping map;

	int cpu;
	int rxq;

	if (copy_from_user(&map, map_user, sizeof(map))) {
		log_err("copy_from_user() failed\n");
		return -EFAULT;
	}

	for (cpu = 0; cpu < SN_MAX_CPU; cpu++) {
		if (map.cpu_to_txq[cpu] >= dev->num_txq) {
			log_err("CPU %d is mapped to a wrong TXQ %d\n",
					cpu, map.cpu_to_txq[cpu]);
			return -EINVAL;
		}
	}

	for (rxq = 0; rxq < dev->num_rxq; rxq++) {
		if (cpu_is_offline(map.rxq_to_cpu[rxq])) {
			log_err("RXQ %d is mapped to a wrong CPU %d\n",
					rxq, map.rxq_to_cpu[rxq]);
			return -EINVAL;
		}
	}

	for_each_possible_cpu(cpu) {
		dev->cpu_to_txq[cpu] = 0;
		dev->cpu_to_rxqs[cpu][0] = -1;
	}

	for (cpu = 0; cpu < min(SN_MAX_CPU, NR_CPUS); cpu++)
		dev->cpu_to_txq[cpu] = map.cpu_to_txq[cpu];

	for (rxq = 0; rxq < dev->num_rxq; rxq++) {
		int cnt;

		cpu = map.rxq_to_cpu[rxq];

		for (cnt = 0; dev->cpu_to_rxqs[cpu][cnt] != -1; cnt++)
			/* do nothing */ ;

		dev->cpu_to_rxqs[cpu][cnt] = rxq;
		dev->cpu_to_rxqs[cpu][cnt + 1] = -1;
	}

	/* sn_dump_queue_mapping(dev); */

	return 0;
}

static long
sn_host_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct sn_device *dev = filp->private_data;

	int ret = 0;
	phys_addr_t bar_phys = 0;

	switch(cmd) {
	case SN_IOC_CREATE_HOSTNIC:
		if (copy_from_user(&bar_phys, (void *)arg, sizeof(bar_phys))) {
			log_err("copy_from_user: %llx", bar_phys);
			return -EINVAL;
		}
		if (dev) {
			ret = -EEXIST;
		} else {
			ret = sn_host_ioctl_create_netdev(
				bar_phys,
				(struct sn_device **)&filp->private_data);
		}
		break;

	case SN_IOC_RELEASE_HOSTNIC:
		if (dev) {
			sn_host_release(NULL, filp);
		} else
			ret = -ENODEV;
		break;

	case SN_IOC_KICK_RX:
		if (dev)
			ret = sn_host_ioctl_kick_rx(dev, arg);
		else
			ret = -ENODEV;
		break;

	case SN_IOC_SET_QUEUE_MAPPING:
		if (dev)
			ret = sn_host_ioctl_set_queue_mapping(dev,
				(struct sn_ioc_queue_mapping __user *) arg);
		else
			ret = -ENODEV;
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct file_operations sn_host_fops = {
	.owner = THIS_MODULE,
	.open = sn_host_open,
	.release = sn_host_release,
	.unlocked_ioctl = sn_host_ioctl,
	.compat_ioctl = sn_host_ioctl,
};

struct miscdevice sn_host_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODULE_NAME,
	.fops = &sn_host_fops,
	.mode = S_IRUSR | S_IRGRP | S_IWUSR | S_IWGRP,
};
