#ifndef _PKTBATCH_H_
#define _PKTBATCH_H_

#include <string.h>

#include <rte_memcpy.h>

#define MAX_PKT_BURST			32

struct snbuf;

struct pkt_batch {
	int cnt;
	struct snbuf * restrict pkts[MAX_PKT_BURST];
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

static inline void batch_copy(struct pkt_batch *dst, 
		const struct pkt_batch *src)
{
	int cnt = src->cnt;

	dst->cnt = cnt;
	rte_memcpy((void *)dst->pkts, (void *)src->pkts, 
			cnt * sizeof(struct snbuf *));
}

#endif
