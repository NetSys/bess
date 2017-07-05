// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
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

#ifndef __SN_H__
#define __SN_H__

#include <stdint.h>

#include <rte_config.h>
#include <rte_mbuf.h>

#include "../kmod/sn_common.h"
#include "../kmod/llring.h"
#include "../snbuf.h"

#define IFNAMSIZ 16
#define ETH_ALEN 6

#define MAX_QUEUES 128

#define APPNAMESIZ 16

/* Ideally share this with vport driver */

#define PORT_NAME_LEN 128
#define PORT_FNAME_LEN 128 + 256

#define MAX_QUEUES_PER_PORT_DIR 32

#define VPORT_DIR_PREFIX "sn_vports"

#ifndef __cacheline_aligned
#define __cacheline_aligned __attribute__((aligned(64)))
#endif

struct vport_inc_regs {
	uint64_t dropped;
} __cacheline_aligned;

struct vport_out_regs {
	volatile uint32_t irq_enabled;
} __cacheline_aligned;

/* This is equivalent to the old bar */
struct vport_bar {
	char name[PORT_NAME_LEN];

	/* The term RX/TX could be very confusing for a virtual switch.
	 * Instead, we use the "incoming/outgoing" convention:
	 * - incoming: outside -> BESS
	 * - outgoing: BESS -> outside */
	int num_inc_q;
	int num_out_q;

	struct vport_inc_regs *inc_regs[MAX_QUEUES_PER_PORT_DIR];
	struct llring *inc_qs[MAX_QUEUES_PER_PORT_DIR];

	struct vport_out_regs *out_regs[MAX_QUEUES_PER_PORT_DIR];
	struct llring *out_qs[MAX_QUEUES_PER_PORT_DIR];
};

struct sn_port {
	struct vport_bar *bar;

	int num_txq;
	int num_rxq;

	struct vport_inc_regs *tx_regs[MAX_QUEUES_PER_PORT_DIR];
	struct llring *tx_qs[MAX_QUEUES_PER_PORT_DIR];

	struct vport_out_regs *rx_regs[MAX_QUEUES_PER_PORT_DIR];
	struct llring *rx_qs[MAX_QUEUES_PER_PORT_DIR];

	int fd[MAX_QUEUES_PER_PORT_DIR];
};

/* End ideally share this part */

//// Maximum size of a metadata attribute or value
// extern const int ATTRSZ;

extern struct rte_mbuf rte_mbuf_template;

void eal_thread_init_master(unsigned lcore_id);

void sn_init_thread(uint32_t lcore);

uint32_t sn_get_lcore_id();

void init_bess(uint32_t lcore, char *name);
struct sn_port *init_port(const char *ifname);
void close_port(struct sn_port *port);
int sn_receive_pkts(struct sn_port *port, int rxq, struct snbuf **pkts,
		    int cnt);
int sn_send_pkts(struct sn_port *port, int txq, struct snbuf **pkts, int cnt);
void sn_snb_free(struct snbuf *pkt);
struct snbuf *sn_snb_alloc(void);
void sn_snb_alloc_bulk(snb_array_t snbs, int cnt);
void sn_snb_free_bulk(snb_array_t snbs, int cnt);
void sn_snb_free_bulk_range(snb_array_t snbs, int start, int cnt);
void sn_snb_copy_batch(snb_array_t src, snb_array_t dest, int cnt);

void sn_enable_interrupt(struct vport_out_regs *);
void sn_disable_interrupt(struct vport_out_regs *);

uint16_t sn_num_txq(struct sn_port *vport);

uint16_t sn_num_rxq(struct sn_port *vport);

uint64_t sn_rdtsc();

void sn_wait(long cycles);

/** Push metadata onto the packet.
 *
 * @param pkt the packet buffer.
 * @param key the key to push. Must be a null terminated string of size
 * of up to ATTRSZ.
 * @param value the valu eto push. Must be a null terminated string of size
 * of up to ATTRSZ.
 *
 * @return 0 on success, -1 on failure.
 */
// int push_metadata_tag(struct snbuf *pkt,
// const char *key,
// const char *value);

extern struct rte_mempool *mempool;

static inline int __receive_pkts(struct sn_port *port, int rxq,
				 struct snbuf **pkts, int cnt)
{
	return llring_dequeue_burst(port->rx_qs[rxq], (void **)pkts, cnt);
}

static inline int __send_pkts(struct sn_port *port, int txq,
			      struct snbuf **pkts, int cnt)
{
	int sent;

	sent = llring_enqueue_burst(port->tx_qs[txq], (void **)pkts, cnt) &
	       (~RING_QUOT_EXCEED);

	port->tx_regs[txq]->dropped += (cnt - sent);

	return sent;
}

static inline void __sn_enable_interrupt(struct vport_out_regs *rx_regs)
{
	__sync_synchronize();
	rx_regs->irq_enabled = 1;
	__sync_synchronize();
}

static inline void __sn_disable_interrupt(struct vport_out_regs *rx_regs)
{
	rx_regs->irq_enabled = 0;
}

static inline struct snbuf *__sn_snb_alloc()
{
	struct snbuf *pkt = (struct snbuf *)rte_pktmbuf_alloc(mempool);

	return pkt;
}

static inline void __sn_snb_free(struct snbuf *pkt) { snb_free(pkt); }

static inline void __sn_snb_free_bulk(snb_array_t pkts, int cnt)
{
	struct rte_mempool *pool = pkts[0]->mbuf.pool;

	int i;

	for (i = 0; i < cnt; i++) {
		struct rte_mbuf *mbuf = &pkts[i]->mbuf;

		if (unlikely(mbuf->pool != pool || !snb_is_simple(pkts[i]) ||
			     rte_mbuf_refcnt_read(mbuf) != 1)) {
			goto slow_path;
		}
	}

	/* NOTE: it seems that zeroing the refcnt of mbufs is not necessary.
	 *   (allocators will reset them) */
	rte_mempool_put_bulk(pool, (void **)pkts, cnt);
	return;

slow_path:
	for (i = 0; i < cnt; i++)
		snb_free(pkts[i]);
}

static inline void __sn_snb_alloc_bulk(snb_array_t snbs, int cnt)
{
	int ret;
	int i;

#if PARANOIAC_OPTIMIZATION
	__m128 mbuf_template;

	mbuf_template = *((__m128 *)&rte_mbuf_template.buf_len);
#endif
	ret = rte_mempool_get_bulk(mempool, (void **)snbs, cnt);
	assert(ret == 0);

	for (i = 0; i < cnt; i++) {
		struct snbuf *snb = snbs[i];
#if PARANOIAC_OPTIMIZATION
		*((__m128 *)&snb->mbuf.buf_len) = mbuf_template;
		*((__m128 *)&snb->mbuf.packet_type) = _mm_setzero_ps();
#else
		rte_mbuf_refcnt_set(&snb->mbuf, 1);
		rte_pktmbuf_reset(&snb->mbuf);
#endif
	}
}

#endif
