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

#if 0

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/fs.h>

#include "sn.h"
#include "sn_ivshmem.h"

static void *shbar = NULL;

static void sn_reclaim_tx_pkts(struct sn_queue *queue)
{
#define MAX_BATCH 32
	void *objs[MAX_BATCH];
	int i, cnt;
	struct sn_tx_metadata *tx_meta = NULL;
	//reclaim tx completed metadata and skb
	while ((cnt = llring_dequeue_burst(queue->sn_to_drv, objs, MAX_BATCH)) > 0) {
		for (i = 0; i < cnt; i++) {
			tx_meta = phys_to_virt((phys_addr_t)objs[i]);
			if (tx_meta->skb)
				dev_kfree_skb(tx_meta->skb);
			tx_meta->skb = NULL;
			objs[i] = tx_meta;
		}
		sn_stack_push(&queue->ready_tx_meta, objs, cnt);
	}
}
static int sn_guest_do_tx(struct sn_queue *queue, struct sk_buff *skb, 
			  struct sn_tx_metadata *tx_meta)
{
	void *objs[1];
	int ret = 0;
	int i;
	int nr_frags = skb_shinfo(skb)->nr_frags;

	struct sn_tx_metadata *tx_meta_send = NULL;

	if (nr_frags + 1 > SN_TX_FRAG_MAX_NUM) {
		skb_linearize(skb);
		log_info("linearize skb due to too many fragmenets %d\n", nr_frags);
		return NET_XMIT_DROP;
	}
	
	if (sn_stack_pop(&queue->ready_tx_meta, objs, 1) < 0) {
		sn_reclaim_tx_pkts(queue);
		log_err("drop due to lack of preallocated tx metadata\n");
		return NET_XMIT_DROP;
	}
	tx_meta_send = (struct sn_tx_metadata*)objs[0];

	//clone skb so that original skb can be freed and associate socket can reclaim buffer
	skb = skb_clone(skb, GFP_ATOMIC);
	if (skb == NULL) {
		sn_stack_push(&queue->ready_tx_meta, objs, 1);
		return NET_XMIT_DROP;
	}

	//copy metadata and fragments info
	memcpy(tx_meta_send, tx_meta, sizeof(*tx_meta));

	tx_meta_send->frag_addr[0] = virt_to_phys(skb->data);
	tx_meta_send->frag_len[0] = skb_headlen(skb);
	tx_meta_send->nr_frags = nr_frags + 1;

	for (i = 1; i <= nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i - 1];
		tx_meta_send->frag_addr[i] = virt_to_phys(skb_frag_address(frag));
		tx_meta_send->frag_len[i] = skb_frag_size(frag);
	}

	tx_meta_send->skb = skb;

	objs[0] = (void*)virt_to_phys(tx_meta_send);

	ret = llring_enqueue_bulk(queue->drv_to_sn, objs, 1);

	sn_reclaim_tx_pkts(queue);

	if (likely(ret == 0)) { 
		return NET_XMIT_SUCCESS;
	} else if (ret == -LLRING_ERR_NOBUF) {
		objs[0] = tx_meta_send;
		dev_kfree_skb(skb);
		tx_meta_send->skb = NULL;
		sn_stack_push(&queue->ready_tx_meta, objs, 1);
		return NET_XMIT_DROP;
	} else {
		return NET_XMIT_SUCCESS;
	}
}

static void clear_ring(struct sn_queue *queue)
{
	int count;
	void *objs[1];
	count = llring_count(queue->drv_to_sn);
	while (count > 0) {
		llring_dequeue_bulk(queue->drv_to_sn, objs, 1);
		count = llring_count(queue->drv_to_sn);
	}

	count = llring_count(queue->sn_to_drv);
	while (count > 0) {
		llring_dequeue_bulk(queue->sn_to_drv, objs, 1);
		count = llring_count(queue->sn_to_drv);
	}
}


static void cleanup_rxring(struct sn_queue *queue)
{
	int count;
	void *objs[1] = {0};
	struct sk_buff *skb;

	count = llring_count(queue->drv_to_sn);
	while (count > 0) {
		llring_dequeue_bulk(queue->drv_to_sn, objs, 1);

		objs[0] = phys_to_virt((phys_addr_t)objs[0]);
		skb = (struct sk_buff *)(*(void**)((char*)objs[0] - sizeof(void*)));

		dev_kfree_skb(skb);

		count = llring_count(queue->drv_to_sn);
	}

	count = llring_count(queue->sn_to_drv);
	while (count > 0) {
		llring_dequeue_bulk(queue->sn_to_drv, objs, 1);

		objs[0] = phys_to_virt((phys_addr_t)objs[0]);
		skb = (struct sk_buff *)(*(void**)((char*)objs[0] - sizeof(void*)));

		dev_kfree_skb(skb);

		count = llring_count(queue->sn_to_drv);
	}
}


static void cleanup_txring(struct sn_queue *queue)
{
	int count;
	struct sn_tx_metadata *tx_meta;
	void *objs[1] = {0};

	count = llring_count(queue->drv_to_sn);
	while (count > 0) {
		llring_dequeue_bulk(queue->drv_to_sn, objs, 1);

		tx_meta = phys_to_virt((phys_addr_t)objs[0]);
		if (tx_meta->skb)
			dev_kfree_skb(tx_meta->skb);
		kfree(tx_meta);

		count = llring_count(queue->drv_to_sn);
	}

	count = llring_count(queue->sn_to_drv);
	while (count > 0) {
		llring_dequeue_bulk(queue->sn_to_drv, objs, 1);

		tx_meta = phys_to_virt((phys_addr_t)objs[0]);
		if (tx_meta->skb)
			dev_kfree_skb(tx_meta->skb);
		kfree(tx_meta);

		count = llring_count(queue->sn_to_drv);
	}
}

static void populate_tx_ring(struct sn_queue *queue)
{
	struct sn_tx_metadata *tx_meta;

	void *objs[1];

	int i;
	int ret;
	int limit;
	int deficit;

	int count = llring_count(queue->drv_to_sn) + 
		llring_count(queue->sn_to_drv);

	limit = queue->drv_to_sn->common.watermark;
	if (limit == 0) 
		limit = queue->drv_to_sn->common.slots;

	deficit = limit - count;

	log_info("fill %d sn_tx_meta\n", deficit);
	for (i = 0; i < deficit; i++) {
		tx_meta = kmalloc(sizeof(struct sn_tx_metadata), GFP_KERNEL);
		if (!tx_meta)
			break;

		tx_meta->skb = NULL;
		objs[0] = (void*)tx_meta;

		ret = sn_stack_push(&queue->ready_tx_meta, objs, 1);
		if (unlikely(ret < 0)) {
			/* It should never happen! :( */
			log_err("BAD THING HAPPENED\n");
			kfree(tx_meta);
			break;
		}
	}
}

static void populate_rx_ring(struct sn_queue *queue)
{
	struct sk_buff *skb;

	void *objs[1];

	int i;
	int ret;
	int limit;
	int deficit;

	int count = llring_count(queue->drv_to_sn) + 
		llring_count(queue->sn_to_drv);

	limit = queue->drv_to_sn->common.watermark;
	if (limit == 0) 
		limit = queue->drv_to_sn->common.slots;

	deficit = limit - count;

	log_info("fill %d skbs\n", deficit);
	for (i = 0; i < deficit; i++) {
		skb = netdev_alloc_skb(queue->dev->netdev, SNBUF_DATA);
		if (!skb)
			break;

		objs[0] = (void*)virt_to_phys((char*)skb->data + sizeof(void*));
		*(void**)skb->data = skb;
		//log_info("fill skb %p %p\n", skb, objs[0]);
		//objs[0] = 

		ret = llring_enqueue_bulk(queue->drv_to_sn, objs, 1);
		if (unlikely(ret == -LLRING_ERR_NOBUF)) {
			/* It should never happen! :( */
			log_err("BAD THING HAPPENED: skb buffer queue overflow!\n");
			dev_kfree_skb(skb);
			break;
		}
	}
}

static struct sk_buff *sn_guest_do_rx(struct sn_queue *queue, 
				      struct sn_rx_metadata *rx_meta)
{
	struct sk_buff *skb = NULL, *skb2 = NULL;

	void *objs[1];
	int ret;

	ret = llring_dequeue_bulk(queue->sn_to_drv, objs, 1);
	if (ret == -LLRING_ERR_NOENT) {
		return NULL;
	}
	
	objs[0] = phys_to_virt((phys_addr_t)objs[0]);
	skb = (struct sk_buff *)(*(void**)((char*)objs[0] - sizeof(void*)));

	memcpy(rx_meta, skb->data + sizeof(void*), sizeof(struct sn_rx_metadata));

	skb->data += sizeof(struct sn_rx_metadata) + sizeof(void*);
	skb->tail += rx_meta->length + sizeof(struct sn_rx_metadata) + sizeof(void*);
	skb->len += rx_meta->length;

	//refill
	skb2 = netdev_alloc_skb(queue->dev->netdev, SNBUF_DATA);
	if (!skb2)
		return skb;
	//log_info("refill skb %p\n", skb2);
	objs[0] = (void*)virt_to_phys((char*)skb2->data + sizeof(void*));
	*(void**)skb2->data = skb2;

	ret = llring_enqueue_bulk(queue->drv_to_sn, objs, 1);
	if (unlikely(ret == -LLRING_ERR_NOBUF)) {
		/* It should never happen! :( */
		log_err("BAD THING HAPPENED: skb buffer queue overflow!\n");
		dev_kfree_skb(skb2);
	}

	return skb;
}

static bool sn_guest_pending_rx(struct sn_queue *queue)
{
	return llring_empty(queue->sn_to_drv) == 0;
}

static struct sn_ops sn_guest_ops = {
	.do_tx 		= sn_guest_do_tx,
	.do_rx 		= sn_guest_do_rx,
	.pending_rx 	= sn_guest_pending_rx,
};

static int sn_guest_create_netdev(void *bar,
				  struct sn_device **dev_ret)
{
	int ret;

	//log_info("BAR: phys=%p virt=%p\n", (void *)bar_phys, bar);

	ret = sn_create_netdev(bar, dev_ret);
	if (ret)
		return ret;

	(*dev_ret)->type = sn_dev_type_pci;//not sure this
	(*dev_ret)->ops = &sn_guest_ops;
	(*dev_ret)->pdev = NULL;

	ret = sn_register_netdev(bar, *dev_ret);
	if (ret)
		*dev_ret = NULL;

	return ret;
}

static struct sn_device *p_sndev = NULL;


static void interrupt_handler(int itr, u32 msg)
{
	int qid = sn_ivsm_irq_to_qid(itr);
	//unsigned long flags;
	//log_info("interrupt on qid: %d itr: %d\n", qid, itr);

	//qid = qid % num_online_cpus();
	sn_trigger_softirq_with_qid(p_sndev, qid);
	//smp_call_function_single(qid, sn_trigger_softirq, p_sndev, 0);
}

int sn_guest_init(void)
{
	int i;
	int ret;
	void *bar;
	struct sn_device **dev_ret;

	log_info("bess guest module loaded\n");

	ret = sn_ivsm_init();
	if (ret < 0)
		goto deregister;
	
	bar = sn_ivsm_get_start();
	shbar = bar;

	dev_ret = &p_sndev;
	ret = sn_guest_create_netdev(bar, dev_ret);
	if (ret) 
		goto ivsm_cleanup;

	//clean up ring as exisiting items may not belong to this vm.
	for (i = 0; i < p_sndev->num_txq; i++) {
		clear_ring(p_sndev->tx_queues[i]);
	}
	for (i = 0; i < p_sndev->num_rxq; i++) {
		clear_ring(p_sndev->rx_queues[i]);
	}

	//populate rx ring
	for (i = 0; i < p_sndev->num_rxq; i++) {
		populate_rx_ring(p_sndev->rx_queues[i]);
	}

	//populate tx ring
	for (i = 0; i < p_sndev->num_txq; i++) {
		populate_tx_ring(p_sndev->tx_queues[i]);
	}

	//register interrupt handler
	sn_ivsm_register_ih(&interrupt_handler);

	//enable interrupt
	ret = sn_ivsm_register_interrupt(p_sndev->num_rxq);
	if (ret) {
		log_err("interrupt registration failed");
		goto netdev_cleanup;
	}

	return 0;

 netdev_cleanup:
	sn_release_netdev(p_sndev);
 ivsm_cleanup:
	sn_ivsm_cleanup();
 deregister:
	return ret;
}

void sn_guest_cleanup(void)
{
	int i;

	if (p_sndev) {
		for (i = 0; i < p_sndev->num_rxq; i++) {
			cleanup_rxring(p_sndev->rx_queues[i]);
		}
		for (i = 0; i < p_sndev->num_txq; i++) {
			cleanup_txring(p_sndev->tx_queues[i]);
		}

		sn_release_netdev(p_sndev);
		p_sndev = NULL;
	}
	sn_ivsm_cleanup();
}

#endif
