#include <assert.h>

#include <rte_errno.h>

#include <sn.h>

#include "common.h"
#include "time.h"
#include "dpdk.h"
#include "snbuf.h"

/* 2^n - 1 is optimal according to the DPDK manual */
#define NUM_PFRAMES	(16384 - 1)
#define NUM_LFRAMES	(8192 - 1)

#define NUM_SNBUFS	(NUM_PFRAMES + NUM_LFRAMES)

#define NUM_MEMPOOL_CACHE	512

struct snbuf snbuf_template __cacheline_aligned;
struct rte_mbuf pframe_template;
struct rte_mbuf lframe_template;

static struct rte_mempool *lframe_pool[RTE_MAX_NUMA_NODES];
static struct rte_mempool *pframe_pool[RTE_MAX_NUMA_NODES];

static void init_mempool_socket(int sid)
{
	char name[256];
	
	sprintf(name, "pframe%d", sid);
	pframe_pool[sid] = rte_mempool_create(name, 
			NUM_PFRAMES, 
			sizeof(struct rte_mbuf) + \
			  RTE_PKTMBUF_HEADROOM + MAX_PFRAME,
			NUM_MEMPOOL_CACHE, 
			sizeof(struct rte_pktmbuf_pool_private),
			rte_pktmbuf_pool_init, (void*)MAX_PFRAME, rte_pktmbuf_init, NULL,
			sid, 0);
	if (!pframe_pool[sid]) {
		printf("%s\n",rte_strerror(rte_errno));
		assert(0);
	}

	sprintf(name, "lframe%d", sid);
	lframe_pool[sid] = rte_mempool_create(name, 
			NUM_LFRAMES, 
			sizeof(struct rte_mbuf) + \
			  RTE_PKTMBUF_HEADROOM + MAX_LFRAME,
			NUM_MEMPOOL_CACHE, 
			sizeof(struct rte_pktmbuf_pool_private),
			rte_pktmbuf_pool_init, (void*)MAX_LFRAME, rte_pktmbuf_init, NULL,
			sid, 0);

	if (!lframe_pool[sid]) {
		printf("%s\n",rte_strerror(rte_errno));
		assert(0);
	}
}

static void init_templates(void)
{
	int i;

#if OLD_METADATA
	memset(&snbuf_template, 0, sizeof(struct snbuf));

	snbuf_template.in_port = PORT_UNSET;
	snbuf_template.in_queue = QUEUE_UNSET;
	snbuf_template.out_port = PORT_UNSET;
	snbuf_template.out_queue = QUEUE_UNSET;
#endif
	for (i = 0; i < RTE_MAX_NUMA_NODES; i++) {
		struct rte_mbuf *mbuf;

		if (!pframe_pool[i])
			continue;

		mbuf = rte_pktmbuf_alloc(pframe_pool[i]);
		pframe_template = *mbuf;
		rte_pktmbuf_free(mbuf);

		mbuf = rte_pktmbuf_alloc(lframe_pool[i]);
		lframe_template = *mbuf;
		rte_pktmbuf_free(mbuf);
	}
}

void init_mempool(void)
{
	int initialized[RTE_MAX_NUMA_NODES];

	int i;

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

struct rte_mempool *get_lframe_pool()
{
	return lframe_pool[ctx.socket];
}

struct rte_mempool *get_pframe_pool()
{
	return pframe_pool[ctx.socket];
}

struct rte_mempool *get_lframe_pool_socket(int socket)
{
	return lframe_pool[socket];
}

struct rte_mempool *get_pframe_pool_socket(int socket)
{
	return pframe_pool[socket];
}

void snb_dump(FILE *file, struct snbuf *pkt)
{
	struct rte_mbuf *mbuf;

	fprintf(file, "----------------------------------------\n");

#if OLD_METADATA
	fprintf(file, "in_port   = %hhu\n", pkt->in_port);
	fprintf(file, "in_queue  = %hhu\n", pkt->in_queue);
	fprintf(file, "out_port  = %hhu\n", pkt->out_port);
	fprintf(file, "out_queue = %hhu\n", pkt->out_queue);
#endif

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
			if (lframe_pool[i] == mbuf->pool)
				fprintf(file, "L%d", i);
		}
		fprintf(file, ") ");
	}
	fprintf(file, "\n");

	mbuf = (struct rte_mbuf *)pkt;
	rte_pktmbuf_dump(file, mbuf, snb_total_len(pkt));
}

void snb_perf_test()
{
	uint64_t start = rdtsc();

	struct pkt_batch batch;
	int i;

	const int millions = 30;

	batch.cnt = MAX_PKT_BURST;

	printf("running packet alloc/dealloc test...\n");

	for (i = 0; i < 1000000 * millions; i++) {
		snb_alloc_bulk(SNBUF_PFRAME, batch.pkts, batch.cnt, 60);
		snb_free_bulk(batch.pkts, batch.cnt);
	}

	printf("%.1f packets/s\n", 
			(millions * 1000000) * batch.cnt / 
			((double)(rdtsc() - start) / tsc_hz));
}
