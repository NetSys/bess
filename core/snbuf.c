#include <assert.h>

#include <rte_errno.h>

#include <sn.h>

#include "common.h"
#include "opts.h"
#include "log.h"
#include "time.h"
#include "dpdk.h"
#include "snbuf.h"

#define NUM_MEMPOOL_CACHE	512

struct rte_mbuf pframe_template;

static struct rte_mempool *pframe_pool[RTE_MAX_NUMA_NODES];

/* per-packet initializer for mempool */
static void snbuf_pkt_init(struct rte_mempool *mp, void *opaque_arg,
		void *_m, unsigned i)
{
	struct snbuf *snb;
	struct snbuf_immutable *immutable;

	snb = _m;
	immutable = (struct snbuf_immutable *)&snb->immutable;

	rte_pktmbuf_init(mp, NULL, _m, i);

	memset(snb->_reserve, 0, SNBUF_RESERVE);

	immutable->vaddr = snb;
	immutable->paddr = rte_mempool_virt2phy(mp, snb);
	immutable->sid = (uint32_t)(uint64_t)opaque_arg;
	immutable->index = i;
}

static void init_mempool_socket(int sid)
{
	struct rte_pktmbuf_pool_private pool_priv;
	char name[256];

	const int initial_try = 524288;
	const int minimum_try = 16384;
	int current_try = initial_try;

	pool_priv.mbuf_data_room_size = SNBUF_HEADROOM + SNBUF_DATA;
	pool_priv.mbuf_priv_size = SNBUF_RESERVE;

again:
	sprintf(name, "pframe%d_%dk", sid, (current_try + 1) / 1024);

	/* 2^n - 1 is optimal according to the DPDK manual */
	pframe_pool[sid] = rte_mempool_create(name, 
			current_try - 1, 
			sizeof(struct snbuf),
			NUM_MEMPOOL_CACHE, 
			sizeof(struct rte_pktmbuf_pool_private),
			rte_pktmbuf_pool_init, &pool_priv,
			snbuf_pkt_init, (void *)(int64_t)sid, 
			sid, 0);

	if (!pframe_pool[sid]) {
		log_warn("pframe allocation (%d pkts) failure on node %d: %s\n",
				current_try - 1, sid, rte_strerror(rte_errno));
		if (current_try > minimum_try) {
			current_try /= 2;
			goto again;
		}

		log_crit("Packet buffer allocation failed on socket %d\n", sid);
		exit(EXIT_FAILURE);
	}

	log_info("%d packet buffers allocated on socket %d\n", 
			current_try - 1, sid);

	if (global_opts.debug_mode)
		rte_mempool_dump(stdout, pframe_pool[sid]);
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

	assert(SNBUF_IMMUTABLE_OFF == 128);
	assert(SNBUF_METADATA_OFF == 192);
	assert(SNBUF_SCRATCHPAD_OFF == 320);

	if (global_opts.debug_mode)
		rte_dump_physmem_layout(stdout);

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

struct snbuf *paddr_to_snb(phys_addr_t paddr)
{
	struct snbuf *ret = NULL;

	for (int i = 0; i < RTE_MAX_NUMA_NODES; i++) {
		struct rte_mempool *pool;

		phys_addr_t pg_start;
		phys_addr_t pg_end;
		uintptr_t size;

		pool = pframe_pool[i];
		if (!pool)
			continue;

		assert(pool->pg_num == 1);

		pg_start = pool->elt_pa[0];
		size = pool->elt_va_end - pool->elt_va_start;
		pg_end = pg_start + size;

		if (pg_start <= paddr && paddr < pg_end) {
			uintptr_t offset;
			
			offset = paddr - pg_start;
			ret = (struct snbuf *)(pool->elt_va_start + offset);

			if (snb_to_paddr(ret) != paddr)
				log_crit("snb->immutable.paddr "
						"corruption detected\n");

			break;
		}
	}

	return ret;
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
