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

/* NOTE: NEVER use rte_pktmbuf_*() directly, 
 *       unless you know what you are doing */
ct_assert(SNBUF_MBUF == sizeof(struct rte_mbuf));
ct_assert(SNBUF_HEADROOM == RTE_PKTMBUF_HEADROOM);

/* snbuf and mbuf share the same start address, so that we can avoid conversion.
 *
 * Layout (2048 bytes):
 *    Offset	Size	Field
 *  - 0		128	mbuf (SNBUF_ sizeof(struct rte_mbuf))
 *  - 128	128	_headroom (SNBUF_HEADROOM == RTE_PKTMBUF_HEADROOM)
 *  - 256	1536	_data (SNBUF_DATA)
 *  - 1792	64	some read-only fields
 *  - 1856	128	static/dynamic metadata fields
 *  - 1984	64	private area for module/driver's internal use
 *                        (currently used for vport RX/TX descriptors)
 *
 * Stride will be 2112B, because of mempool's per-object header which takes 64B.
 *
 * Invariants:
 *  * When packets are newly allocated, the data should be filled from _data.
 *  * The packet data may reside in the _headroom + _data area, 
 *    but its size must not exceed 1536 (SNBUF_DATA) when passed to a port.
 */
struct snbuf {
	union {
		struct rte_mbuf mbuf;
		char _mbuf[SNBUF_MBUF];
	};

	union {
		char _reserve[SNBUF_RESERVE];

		struct {
			union {
				char _immutable[SNBUF_IMMUTABLE];

				const struct snbuf_immutable {
					/* must be the first field */
					struct snbuf *vaddr;

					phys_addr_t paddr;

					/* socket ID */
					uint32_t sid;

					/* packet index within the pool */
					uint32_t index;
				} immutable;
			};

			union {
				char _metadata[SNBUF_METADATA];

				struct {
					/* static fields */

					/* Set (1) iff all of the
					 * following conditions are met:
					 *  1. refcnt == 1
					 *  2. direct mbuf
					 *  3. linear (non-chained) */
					uint8_t simple;

					/* user-defined dynamic fields */
					char _metadata_buf[0]	__ymm_aligned;
				};
			};

			/* used for module/driver-specific data */
			char _scratchpad[SNBUF_SCRATCHPAD];
		};
	};

	char _headroom[SNBUF_HEADROOM];
	char _data[SNBUF_DATA];
};

typedef struct snbuf * restrict * restrict snb_array_t;

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

/* add bytes to the beginning */
static inline char *snb_prepend(struct snbuf *snb, uint16_t len)
{
	if (unlikely(snb->mbuf.data_off < len))
		return NULL;

	snb->mbuf.data_off -= len;
	snb->mbuf.data_len += len;
	snb->mbuf.pkt_len += len;

	return snb_head_data(snb);
}

/* remove bytes from the beginning */
static inline char *snb_adj(struct snbuf *snb, uint16_t len)
{
	if (unlikely(snb->mbuf.data_len < len))
		return NULL;

	snb->mbuf.data_off += len;
	snb->mbuf.data_len -= len;
	snb->mbuf.pkt_len -= len;

	return snb_head_data(snb);
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

struct rte_mempool *get_pframe_pool();
struct rte_mempool *get_pframe_pool_socket(int socket);

static inline phys_addr_t snb_to_paddr(struct snbuf *snb)
{
	return snb->immutable.paddr;
}

/* Slow. Do not use in the datapath */
struct snbuf *paddr_to_snb(phys_addr_t paddr);

void snb_dump(FILE *file, struct snbuf *pkt);

void init_mempool(void);
void close_mempool(void);

#endif
