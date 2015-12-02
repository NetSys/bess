#include <assert.h>

#include <rte_errno.h>

#include <sn.h>

#include "common.h"
#include "time.h"
#include "dpdk.h"
#include "snbuf.h"

/* 2^n - 1 is optimal according to the DPDK manual */
#define NUM_PFRAMES	(131072 - 1)

#define NUM_MEMPOOL_CACHE	512

struct rte_mbuf pframe_template;

static struct rte_mempool *pframe_pool[RTE_MAX_NUMA_NODES];

static void snbuf_pool_init(struct rte_mempool *mp, void *opaque_arg,
		void *_m, unsigned i)
{
	struct snbuf *snb;
	struct snbuf_immutable *immutable;

	snb = _m;
	immutable = (struct snbuf_immutable *)&snb->immutable;

	rte_pktmbuf_init(mp, NULL, _m, i);

	memset(snb->_reserve, 0, SNBUF_TAIL_RESERVE);

	/* Exclude tail area, which is not for packet payload.
	 * NOTE: Do not rte_pktmbuf_detach(), since it resets the field. */
	snb->mbuf.buf_len = SNBUF_HEADROOM + SNBUF_DATA;

	immutable->vaddr = snb;
	immutable->paddr = rte_mempool_virt2phy(mp, snb);

	immutable->sid = (uint32_t)(uint64_t)opaque_arg;
	immutable->index = i;

	snb->simple = 1;
}

static void init_mempool_socket(int sid)
{
	char name[256];
	
	sprintf(name, "pframe%d", sid);
	pframe_pool[sid] = rte_mempool_create(name, 
			NUM_PFRAMES, 
			sizeof(struct rte_mbuf) + \
			  SNBUF_HEADROOM + SNBUF_DATA + SNBUF_TAIL_RESERVE,
			NUM_MEMPOOL_CACHE, 
			sizeof(struct rte_pktmbuf_pool_private),
			rte_pktmbuf_pool_init, 
			(void *)(SNBUF_HEADROOM + SNBUF_DATA), 
			snbuf_pool_init, (void *)(int64_t)sid, 
			sid, 0);

	if (!pframe_pool[sid]) {
		fprintf(stderr, "pframe allocation failure on socket %d: %s\n",
				sid, rte_strerror(rte_errno));
		exit(EXIT_FAILURE);
	}

	/* rte_mempool_dump(stdout, pframe_pool[sid]); */
}

static void init_templates(void)
{
	int i;

	for (i = 0; i < RTE_MAX_NUMA_NODES; i++) {
		struct rte_mbuf *mbuf;

		if (!pframe_pool[i])
			continue;

		mbuf = rte_pktmbuf_alloc(pframe_pool[i]);
		pframe_template = *mbuf;
		rte_pktmbuf_free(mbuf);
	}
}

void init_mempool(void)
{
	int initialized[RTE_MAX_NUMA_NODES];

	int i;

	assert(SNBUF_IMMUTABLE_OFF == 1792);
	assert(SNBUF_METADATA_OFF == 1856);
	assert(SNBUF_SCRATCHPAD_OFF == 1984);

	for (i = 0; i < RTE_MAX_NUMA_NODES; i++)
		initialized[i] = 0;

	for (i = 0; i < RTE_MAX_LCORE; i++) {
		int sid = rte_lcore_to_socket_id(i);

		if (!initialized[sid]) {
			init_mempool_socket(sid);
			initialized[sid] = 1;
		}
	}

	init_templates();
}

void close_mempool(void)
{
	/* Do nothing. Surprisingly, there is no destructor for mempools */
}

struct rte_mempool *get_pframe_pool()
{
	return pframe_pool[ctx.socket];
}

struct rte_mempool *get_pframe_pool_socket(int socket)
{
	return pframe_pool[socket];
}

void snb_dump(FILE *file, struct snbuf *pkt)
{
	struct rte_mbuf *mbuf;

	fprintf(file, "----------------------------------------\n");

	fprintf(file, "refcnt chain: ");
	for (mbuf = (struct rte_mbuf *)pkt; mbuf; mbuf = mbuf->next)
		fprintf(file, "%hu ", mbuf->refcnt);
	fprintf(file, "\n");

	fprintf(file, "pool chain: ");
	for (mbuf = (struct rte_mbuf *)pkt; mbuf; mbuf = mbuf->next) {
		int i;

		fprintf(file, "%p(", mbuf->pool);

		for (i = 0; i < RTE_MAX_NUMA_NODES; i++) {
			if (pframe_pool[i] == mbuf->pool)
				fprintf(file, "P%d", i);
		}
		fprintf(file, ") ");
	}
	fprintf(file, "\n");

	mbuf = (struct rte_mbuf *)pkt;
	rte_pktmbuf_dump(file, mbuf, snb_total_len(pkt));
}
