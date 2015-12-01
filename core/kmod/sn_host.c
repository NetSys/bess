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

#include "sn.h"

static void 
sn_dump_queue_mapping(struct sn_device *dev) __attribute__((unused));

/* User applications are expected to open /dev/softnic every time
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

static int sn_host_do_tx(struct sn_queue *queue, struct sk_buff *skb,
			 struct sn_tx_metadata *tx_meta)
{
	void *objs[2];
	void *cookie;
	char *dst_addr;

	int nr_frags = skb_shinfo(skb)->nr_frags;
	int ret;
	int i;

	ret = llring_mc_dequeue_bulk(queue->sn_to_drv, objs, 2);
	if (unlikely(ret == -LLRING_ERR_NOENT)) {
		queue->tx_stats.descriptor++;
		return NET_XMIT_DROP;
	}

	cookie = objs[0];
	dst_addr = phys_to_virt((phys_addr_t) objs[1]);

	memcpy(dst_addr, tx_meta, sizeof(struct sn_tx_metadata));
	dst_addr += sizeof(struct sn_tx_metadata);

	memcpy(dst_addr, skb->data, skb_headlen(skb));
	dst_addr += skb_headlen(skb);

	for (i = 0; i < nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		memcpy(dst_addr, skb_frag_address(frag), skb_frag_size(frag));
		dst_addr += skb_frag_size(frag);
	}

	ret = llring_mp_enqueue(queue->drv_to_sn, objs[0]);
	if (likely(ret == 0 || ret == -LLRING_ERR_QUOT))
		return (ret == 0) ? NET_XMIT_SUCCESS : NET_XMIT_CN;
	else {
		/* Now it should never happen since refill is throttled.
		 * If it does, the mbuf(objs[0]) leaks. Ouch. */
		pr_err("queue %d is overflowing!\n", queue->queue_id);
		return NET_XMIT_DROP;
	}
}

static int sn_host_do_rx_batch(struct sn_queue *queue,
			       struct sn_rx_metadata *rx_meta,
			       struct sk_buff **skbs,
			       int max_cnt)
{
	void *objs[MAX_RX_BATCH];
	void *cookies[MAX_RX_BATCH];

	int cnt;
	int ret;
	int i;

	if (unlikely(max_cnt > MAX_RX_BATCH))
		max_cnt = MAX_RX_BATCH;

	cnt = llring_sc_dequeue_burst(queue->sn_to_drv, objs, max_cnt);
	if (cnt == 0)
		return 0;

	for (i = 0; i < cnt; i++) {
		struct sk_buff *skb;
		struct sn_rx_desc *rx_desc;

		int total_len;
		int copied;
		char *ptr;

		rx_desc = phys_to_virt((phys_addr_t) objs[i]);
		
		cookies[i] = rx_desc->cookie;

		memcpy(&rx_meta[i], &rx_desc->meta, 
				sizeof(struct sn_rx_metadata));

		total_len = rx_desc->total_len;

		skb = skbs[i] = netdev_alloc_skb(queue->dev->netdev, total_len);
		if (unlikely(!skb)) {
			log_err("netdev_alloc_skb() failed\n");
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
			rx_desc = phys_to_virt(rx_desc->next_desc);
		} while (copied < total_len);
	}

	ret = llring_sp_enqueue_bulk(queue->drv_to_sn, cookies, cnt);
	if (unlikely(ret == -LLRING_ERR_NOBUF)) {
		/* It should never happen! :( */
		log_err("BAD THING HAPPENED: free buffer queue overflow!\n");
	}

	return cnt;
}

#if 0
static struct sk_buff *sn_host_do_rx(struct sn_queue *queue,
				     struct sn_rx_metadata *rx_meta)
{
	struct sk_buff *skb;

	void *objs[2];
	void *cookie;
	void *src_addr;

	int ret;

	ret = llring_sc_dequeue_bulk(queue->sn_to_drv, objs, 2);
	if (ret == -LLRING_ERR_NOENT)
		return NULL;

	cookie = objs[0];
	src_addr = phys_to_virt((phys_addr_t) objs[1]);

	memcpy(rx_meta, src_addr, sizeof(struct sn_rx_metadata));

	skb = netdev_alloc_skb(queue->dev->netdev, rx_meta->length);
	if (likely(skb)) {
		int total_len = rx_meta->length;
		int copied = 0;
		char *ptr = skb_put(skb, total_len);

		do {
			rx_meta = (struct sn_rx_metadata *)src_addr;
			memcpy(ptr + copied, 
					src_addr + sizeof(struct sn_rx_metadata),
					rx_meta->host.seg_len);

			copied += rx_meta->host.seg_len;
			src_addr = phys_to_virt(rx_meta->host.seg_next);
		} while (copied < total_len);
	} else
		log_err("netdev_alloc_skb() failed\n");

	ret = llring_sp_enqueue(queue->drv_to_sn, cookie);
	if (unlikely(ret == -LLRING_ERR_NOBUF)) {
		/* It should never happen! :( */
		log_err("BAD THING HAPPENED: free buffer queue overflow!\n");
	}

	return skb;
}
#endif

static bool sn_host_pending_rx(struct sn_queue *queue)
{
	return llring_count(queue->sn_to_drv) > 0;
}

static struct sn_ops sn_host_ops = {
	.do_tx 		= sn_host_do_tx,
#if 0
	.do_rx 		= sn_host_do_rx,
#endif
	.do_rx_batch	= sn_host_do_rx_batch,
	.pending_rx 	= sn_host_pending_rx,
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

	/* log_info("BAR: phys=%p virt=%p\n", (void *)bar_phys, bar); */

	ret = sn_create_netdev(bar, dev_ret);
	if (ret)
		return ret;

	(*dev_ret)->type = sn_dev_type_host;
	(*dev_ret)->ops = &sn_host_ops;
	(*dev_ret)->pdev = NULL;

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

	for (cpu = 0; cpu < SN_MAX_CPU; cpu++)
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

	switch(cmd) {
	case SN_IOC_CREATE_HOSTNIC:
		if (dev) {
			ret = -EEXIST;
		} else {
			ret = sn_host_ioctl_create_netdev(
				(phys_addr_t) arg,
				(struct sn_device **)&filp->private_data);
		}
		break;

	case SN_IOC_RELEASE_HOSTNIC:
		if (dev) {
			sn_release_netdev((struct sn_device *)
					filp->private_data);
			filp->private_data = NULL;
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
};

struct miscdevice sn_host_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODULE_NAME,
	.fops = &sn_host_fops,
	.mode = S_IRUSR | S_IRGRP | S_IWUSR | S_IWGRP,
};
