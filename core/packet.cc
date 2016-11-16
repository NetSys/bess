#include "packet.h"

#include <cassert>

#include <glog/logging.h>
#include <rte_errno.h>

#include "dpdk.h"
#include "opts.h"
#include "utils/common.h"

namespace bess {

struct rte_mbuf pframe_template;

static struct rte_mempool *pframe_pool[RTE_MAX_NUMA_NODES];

#define NUM_MEMPOOL_CACHE 512

static void packet_init(struct rte_mempool *mp, void *opaque_arg, void *_m,
                        unsigned i) {
  Packet *pkt;

  pkt = reinterpret_cast<Packet *>(_m);

  rte_pktmbuf_init(mp, nullptr, _m, i);

  memset(pkt->reserve(), 0, SNBUF_RESERVE);

  pkt->set_vaddr(pkt);
  pkt->set_paddr(rte_mempool_virt2phy(mp, pkt));
  pkt->set_sid(reinterpret_cast<uintptr_t>(opaque_arg));
  pkt->set_index(i);
}

static void init_mempool_socket(int sid) {
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
  pframe_pool[sid] = rte_mempool_create(
      name, current_try - 1, sizeof(Packet), NUM_MEMPOOL_CACHE,
      sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init,
      &pool_priv, packet_init, (void *)(uintptr_t)sid, sid, 0);

  if (!pframe_pool[sid]) {
    LOG(WARNING) << "Allocating " << current_try - 1 << " buffers on socket "
                 << sid << ": Failed (" << rte_strerror(rte_errno) << ")";
    if (current_try > minimum_try) {
      current_try /= 2;
      goto again;
    }

    LOG(FATAL) << "Packet buffer allocation failed on socket " << sid;
  }

  LOG(INFO) << "Allocating " << current_try - 1 << " buffers on socket " << sid
            << ": OK";
}

static void init_templates(void) {
  int i;

  for (i = 0; i < RTE_MAX_NUMA_NODES; i++) {
    struct rte_mbuf *mbuf;

    if (!pframe_pool[i]) continue;

    mbuf = rte_pktmbuf_alloc(pframe_pool[i]);
    pframe_template = *mbuf;
    rte_pktmbuf_free(mbuf);
  }
}

void init_mempool(void) {
  int initialized[RTE_MAX_NUMA_NODES];

  int i;

  assert(SNBUF_IMMUTABLE_OFF == 128);
  assert(SNBUF_METADATA_OFF == 192);
  assert(SNBUF_SCRATCHPAD_OFF == 320);

  if (FLAGS_d) rte_dump_physmem_layout(stdout);

  for (i = 0; i < RTE_MAX_NUMA_NODES; i++) initialized[i] = 0;

  for (i = 0; i < RTE_MAX_LCORE; i++) {
    int sid = rte_lcore_to_socket_id(i);

    if (!initialized[sid]) {
      init_mempool_socket(sid);
      initialized[sid] = 1;
    }
  }

  init_templates();
}

void close_mempool(void) {
  /* Do nothing. Surprisingly, there is no destructor for mempools */
}

struct rte_mempool *get_pframe_pool() {
  return pframe_pool[ctx.socket()];
}

struct rte_mempool *get_pframe_pool_socket(int socket) {
  return pframe_pool[socket];
}

#if DPDK_VER >= DPDK_VER_NUM(16, 7, 0)
static Packet *paddr_to_snb_memchunk(struct rte_mempool_memhdr *chunk,
    phys_addr_t paddr) {
  if (chunk->phys_addr == RTE_BAD_PHYS_ADDR) return nullptr;

  if (chunk->phys_addr <= paddr && paddr < chunk->phys_addr + chunk->len) {
    uintptr_t vaddr;

    vaddr = (uintptr_t)chunk->addr + paddr - chunk->phys_addr;
    return (Packet *)vaddr;
  }

  return nullptr;
}

Packet *Packet::from_paddr(phys_addr_t paddr) {
  for (int i = 0; i < RTE_MAX_NUMA_NODES; i++) {
    struct rte_mempool *pool;
    struct rte_mempool_memhdr *chunk;

    pool = pframe_pool[i];
    if (!pool) continue;

    STAILQ_FOREACH(chunk, &pool->mem_list, next) {
      Packet *pkt = paddr_to_snb_memchunk(chunk, paddr);
      if (!pkt) continue;

      if (pkt->paddr() != paddr) {
        LOG(ERROR) << "pkt->immutable.paddr corruption: pkt=" << pkt
          << ", pkt->immutable.paddr=" << pkt->paddr()
          << " (!= " << paddr << ")";
        return nullptr;
      }

      return pkt;
    }
  }

  return nullptr;
}
#else
Packet *Packet::from_paddr(phys_addr_t paddr) {
  Packet *ret = nullptr;

  for (int i = 0; i < RTE_MAX_NUMA_NODES; i++) {
    struct rte_mempool *pool;

    phys_addr_t pg_start;
    phys_addr_t pg_end;
    uintptr_t size;

    pool = pframe_pool[i];
    if (!pool) continue;

    assert(pool->pg_num == 1);

    pg_start = pool->elt_pa[0];
    size = pool->elt_va_end - pool->elt_va_start;
    pg_end = pg_start + size;

    if (pg_start <= paddr && paddr < pg_end) {
      uintptr_t offset;

      offset = paddr - pg_start;
      ret = (Packet *)(pool->elt_va_start + offset);

      if (ret->paddr() != paddr)
        log_err(
            "snb->immutable.paddr "
            "corruption detected\n");

      break;
    }
  }

  return ret;
}
#endif

void Packet::Dump(FILE *file) {
  struct rte_mbuf *mbuf;

  fprintf(file, "refcnt chain: ");
  for (mbuf = (struct rte_mbuf *)this; mbuf; mbuf = mbuf->next)
    fprintf(file, "%hu ", mbuf->refcnt);
  fprintf(file, "\n");

  fprintf(file, "pool chain: ");
  for (mbuf = (struct rte_mbuf *)this; mbuf; mbuf = mbuf->next) {
    int i;

    fprintf(file, "%p(", mbuf->pool);

    for (i = 0; i < RTE_MAX_NUMA_NODES; i++) {
      if (pframe_pool[i] == mbuf->pool) fprintf(file, "P%d", i);
    }
    fprintf(file, ") ");
  }
  fprintf(file, "\n");

  mbuf = (struct rte_mbuf *)this;
  rte_pktmbuf_dump(file, mbuf, total_len());
}

} // namespace bess
