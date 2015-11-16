#ifndef _SNBUF_H_
#define _SNBUF_H_

#include <assert.h>

#include <rte_config.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include <sn.h>

#include "debug.h"
#include "worker.h"
#include "dpdk.h"

#include "utils/simd.h"

#define OLD_METADATA			0

#define MAX_SNBUF_BYTES			128
ct_assert(MAX_SNBUF_BYTES <= RTE_PKTMBUF_HEADROOM);

#define MAX_PKT_BURST			32

/* snbuf and mbuf share the same start address, so that we can avoid conversion.
 * The actual content (SNBUF_SIZE) of snbuf resides right after mbuf (128B), 
 * at the beginning of the buffer headroom (RTE_PKTMBUF_HEADROOM) */
struct snbuf {
	struct rte_mbuf mbuf;

	MARKER _snbuf_start;
#if OLD_METADATA

	port_t 	in_port;
	queue_t in_queue;
	port_t 	out_port;
	queue_t out_queue;

	uint16_t l3_offset;	/* 0 for unknown L3 or broken L3 */
	uint16_t l3_protocol;	/* ETHER_TYPE_* in host order. also can be 0 */
	uint16_t l4_offset;	/* 0 for unknown L3 or broken L3/4 */
	uint8_t  l4_protocol;	/* IPPROTO_*. 0 for unknown L3 or broken L3/4 */

	union {
		struct {
			uint16_t csum_start;
			uint16_t csum_dest;
		} tx;

		struct {
			uint16_t gso_mss;
			uint8_t csum_state;
		} rx;
	};
#else
	char metadata_tmp1[0];
#endif

	MARKER _snbuf_end;
};

#define SNBUF_SIZE (offsetof(struct snbuf, _snbuf_end) - \
		offsetof(struct snbuf, _snbuf_start))

typedef struct snbuf * restrict * restrict snb_array_t;

struct pkt_batch {
	struct snbuf * restrict pkts[MAX_PKT_BURST];
	int cnt;
};

static inline void batch_clear(struct pkt_batch *batch)
{
	batch->cnt = 0;
}

static inline void batch_add(struct pkt_batch *batch, struct snbuf *snb)
{
	batch->pkts[batch->cnt++] = snb;
}

static inline int batch_full(struct pkt_batch *batch)
{
	return (batch->cnt == MAX_PKT_BURST);
}

static inline char *snb_head_data(struct snbuf *snb)
{
	return rte_pktmbuf_mtod(&snb->mbuf, char *);
}

static inline int snb_head_len(struct snbuf *snb)
{
	return rte_pktmbuf_data_len(&snb->mbuf);
}

static inline int snb_total_len(struct snbuf *snb)
{
	return rte_pktmbuf_pkt_len(&snb->mbuf);
}

/* single segment? */
static inline int snb_is_linear(struct snbuf *snb)
{
	return rte_pktmbuf_is_contiguous(&snb->mbuf);
}

/* single segment and direct? */
static inline int snb_is_simple(struct snbuf *snb)
{
	return snb_is_linear(snb) && RTE_MBUF_DIRECT(&snb->mbuf);
}

extern struct rte_mbuf pframe_template;

#if OLD_METADATA
extern struct snbuf snbuf_template;		/* global template */

static inline struct snbuf *snb_init_template(struct rte_mbuf *mbuf,
		const struct snbuf *snb_template)
{
	struct snbuf *snb = (struct snbuf *)mbuf;

	ct_assert(SNBUF_SIZE <= MAX_SNBUF_BYTES);

	if (SNBUF_SIZE == 16) {
		*((__m128 *)&snb->_snbuf_start) = 
			*((__m128 *)&snb_template->_snbuf_start);
	} else {
		rte_memcpy(&snb->_snbuf_start, &snb_template->_snbuf_start,
				SNBUF_SIZE);
	}

	return snb;
}

static inline struct snbuf *snb_init(struct rte_mbuf *mbuf)
{
	return snb_init_template(mbuf, &snbuf_template);
}
#endif

static inline struct snbuf *__snb_alloc()
{
	return (struct snbuf *)rte_pktmbuf_alloc(ctx.pframe_pool);
}

static inline struct snbuf *__snb_alloc_pool(struct rte_mempool *pool)
{
	struct rte_mbuf *mbuf;

	mbuf = rte_pktmbuf_alloc(pool);

	return (struct snbuf *)mbuf;
}

static inline struct snbuf *snb_alloc()
{
	struct snbuf *snb = __snb_alloc();

#if OLD_METADATA
	snb_init((struct rte_mbuf *)snb);
#endif

	return snb;
}

static inline void snb_free(struct snbuf *snb)
{
	rte_pktmbuf_free((struct rte_mbuf *)snb);
}

#if __AVX__
#  include "snbuf_avx.h"
#else
static inline int snb_alloc_bulk(snb_array_t snbs, int cnt, uint16_t len)
{
	int ret;
	int i;

	ret = rte_mempool_get_bulk(ctx.pframe_pool, (void **)snbs, cnt);
	if (ret != 0)
		return 0;

	for (i = 0; i < cnt; i++) {
		struct snbuf * __restrict snb = snbs[i];

		rte_mbuf_refcnt_set(&snb->mbuf, 1);
		rte_pktmbuf_reset(&snb->mbuf);
#if OLD_METADATA
		snb_init((struct rte_mbuf *)snb);
#endif
		snb->mbuf.pkt_len = snb->mbuf.data_len = len;
	}

	return cnt;
}

static inline void snb_free_bulk(snb_array_t snbs, int cnt)
{
	struct rte_mempool *pool = snbs[0]->mbuf.pool;
		
	int i;

	for (i = 0; i < cnt; i++) {
		struct rte_mbuf *mbuf = &snbs[i]->mbuf;

		if (unlikely(mbuf->pool != pool || 
				!snb_is_simple(snbs[i]) ||
				rte_mbuf_refcnt_read(mbuf) != 1))
		{
			goto slow_path;
		}
	}

	/* NOTE: it seems that zeroing the refcnt of mbufs is not necessary.
	 *   (allocators will reset them) */
	rte_mempool_put_bulk(pool, (void **)snbs, cnt);
	return;

slow_path:
	for (i = 0; i < cnt; i++)
		snb_free(snbs[i]);
}
#endif

static inline struct snbuf *snb_alloc_with_metadata(struct snbuf *src)
{
	struct snbuf *dst;

	dst = __snb_alloc();
	rte_memcpy(dst->_snbuf_start, src->_snbuf_start, SNBUF_SIZE);

	return dst;
}


/* add bytes to the beginning */
static inline char *snb_prepend(struct snbuf *snb, uint16_t len)
{
	return rte_pktmbuf_prepend(&snb->mbuf, len);
}

/* remove bytes from the beginning */
static inline char *snb_adj(struct snbuf *snb, uint16_t len)
{
	return rte_pktmbuf_adj(&snb->mbuf, len);
}

/* add bytes to the end */
static inline char *snb_append(struct snbuf *snb, uint16_t len)
{
	return rte_pktmbuf_append(&snb->mbuf, len);
}

/* remove bytes from the end */
static inline void snb_trim(struct snbuf *snb, uint16_t to_remove)
{
	int ret;
	
	ret = rte_pktmbuf_trim(&snb->mbuf, to_remove);
	assert(ret == 0);
}

static inline struct snbuf *snb_copy(struct snbuf *src)
{
	struct snbuf *dst;

	assert(snb_is_linear(src));

	dst = __snb_alloc_pool(src->mbuf.pool);

	rte_memcpy(snb_append(dst, snb_total_len(src)),
			snb_head_data(src),
			snb_total_len(src));

	rte_memcpy(dst->_snbuf_start, src->_snbuf_start, 
			SNBUF_SIZE);

	return dst;
}

static inline phys_addr_t snb_seg_dma_addr(struct rte_mbuf *mbuf)
{
	return mbuf->buf_physaddr + mbuf->data_off;
}

static inline phys_addr_t snb_dma_addr(struct snbuf *snb)
{
	return snb_seg_dma_addr(&snb->mbuf);
}

#if OLD_METADATA
static inline struct ipv4_hdr *snb_ipv4(struct snbuf *snb)
{
	if (!snb->l3_offset || snb->l3_protocol != ETHER_TYPE_IPv4)
		return NULL;
	
	return (struct ipv4_hdr *)(snb_head_data(snb) + snb->l3_offset);
}

static inline struct tcp_hdr *snb_tcp(struct snbuf *snb)
{
	if (!snb->l4_offset || snb->l4_protocol != IPPROTO_TCP)
		return NULL;

	return (struct tcp_hdr *)(snb_head_data(snb) + snb->l4_offset);
}

static inline struct udp_hdr *snb_udp(struct snbuf *snb)
{
	if (!snb->l4_offset || snb->l4_protocol != IPPROTO_UDP)
		return NULL;

	return (struct udp_hdr *)(snb_head_data(snb) + snb->l4_offset);
}

/* when we need only port numbers... */
static inline struct udp_hdr *snb_udptcp(struct snbuf *snb)
{
	if (!snb->l4_offset || (snb->l4_protocol != IPPROTO_UDP &&
				snb->l4_protocol != IPPROTO_TCP))
		return NULL;

	return (struct udp_hdr *)(snb_head_data(snb) + snb->l4_offset);
}
#endif

struct rte_mempool *get_pframe_pool();
struct rte_mempool *get_pframe_pool_socket(int socket);

void snb_dump(FILE *file, struct snbuf *pkt);

void init_mempool(void);
void close_mempool(void);

#endif
