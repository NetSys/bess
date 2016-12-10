#ifndef BESS_SNBUF_H_
#define BESS_SNBUF_H_

#include <rte_config.h>
#include <rte_mbuf.h>

#include <cassert>

#include "metadata.h"
#include "worker.h"

#include "snbuf_layout.h"

/* NOTE: NEVER use rte_pktmbuf_*() directly,
 *       unless you know what you are doing */
static_assert(SNBUF_MBUF == sizeof(struct rte_mbuf),
              "DPDK compatibility check failed");
static_assert(SNBUF_HEADROOM == RTE_PKTMBUF_HEADROOM,
              "DPDK compatibility check failed");

// For the layout of snbuf, see snbuf_layout.h
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

        const struct {
          /* must be the first field */
          struct snbuf *vaddr;

          phys_addr_t paddr;

          /* socket ID */
          uint32_t sid;

          /* packet index within the pool */
          uint32_t index;
        };
      };

      /* Dynamic metadata.
       * Each attribute value is stored in host order */
      char _metadata[SNBUF_METADATA];

      /* Used for module/driver-specific data */
      char _scratchpad[SNBUF_SCRATCHPAD];
    };
  };

  char _headroom[SNBUF_HEADROOM];
  char _data[SNBUF_DATA];
};

typedef struct snbuf **snb_array_t;

static inline void *snb_head_data(struct snbuf *snb) {
  return rte_pktmbuf_mtod(&snb->mbuf, void *);
}

static inline int snb_head_len(struct snbuf *snb) {
  return rte_pktmbuf_data_len(&snb->mbuf);
}

static inline int snb_total_len(struct snbuf *snb) {
  return rte_pktmbuf_pkt_len(&snb->mbuf);
}

/* single segment? */
static inline int snb_is_linear(struct snbuf *snb) {
  return rte_pktmbuf_is_contiguous(&snb->mbuf);
}

/* single segment and direct? */
static inline int snb_is_simple(struct snbuf *snb) {
  return snb_is_linear(snb) && RTE_MBUF_DIRECT(&snb->mbuf);
}

extern struct rte_mbuf pframe_template;

static inline struct snbuf *__snb_alloc() {
  return (struct snbuf *)rte_pktmbuf_alloc(ctx.pframe_pool());
}

static inline struct snbuf *__snb_alloc_pool(struct rte_mempool *pool) {
  struct rte_mbuf *mbuf;

  mbuf = rte_pktmbuf_alloc(pool);

  return (struct snbuf *)mbuf;
}

static inline struct snbuf *snb_alloc() {
  struct snbuf *snb = __snb_alloc();

  return snb;
}

static inline void snb_free(struct snbuf *snb) {
  rte_pktmbuf_free((struct rte_mbuf *)snb);
}

#if __AVX__
#include "snbuf_avx.h"
#else
static inline int snb_alloc_bulk(snb_array_t snbs, int cnt, uint16_t len) {
  int ret;
  int i;

  ret = rte_mempool_get_bulk(ctx.pframe_pool(), reinterpret_cast<void **>(snbs),
                             cnt);
  if (ret != 0) {
    return 0;
  }

  for (i = 0; i < cnt; i++) {
    struct snbuf *snb = snbs[i];

    rte_mbuf_refcnt_set(&snb->mbuf, 1);
    rte_pktmbuf_reset(&snb->mbuf);

    snb->mbuf.pkt_len = snb->mbuf.data_len = len;
  }

  return cnt;
}

static inline void snb_free_bulk(snb_array_t snbs, int cnt) {
  struct rte_mempool *pool = snbs[0]->mbuf.pool;

  int i;

  for (i = 0; i < cnt; i++) {
    struct rte_mbuf *mbuf = &snbs[i]->mbuf;

    if (unlikely(mbuf->pool != pool || !snb_is_simple(snbs[i]) ||
                 rte_mbuf_refcnt_read(mbuf) != 1)) {
      goto slow_path;
    }
  }

  /* NOTE: it seems that zeroing the refcnt of mbufs is not necessary.
   *   (allocators will reset them) */
  rte_mempool_put_bulk(pool, reinterpret_cast<void **>(snbs), cnt);
  return;

slow_path:
  for (i = 0; i < cnt; i++) {
    snb_free(snbs[i]);
  }
}
#endif

/* add bytes to the beginning */
static inline void *snb_prepend(struct snbuf *snb, uint16_t len) {
  if (unlikely(snb->mbuf.data_off < len)) {
    return nullptr;
  }

  snb->mbuf.data_off -= len;
  snb->mbuf.data_len += len;
  snb->mbuf.pkt_len += len;

  return snb_head_data(snb);
}

/* remove bytes from the beginning */
static inline void *snb_adj(struct snbuf *snb, uint16_t len) {
  if (unlikely(snb->mbuf.data_len < len)) {
    return nullptr;
  }

  snb->mbuf.data_off += len;
  snb->mbuf.data_len -= len;
  snb->mbuf.pkt_len -= len;

  return snb_head_data(snb);
}

/* add bytes to the end */
static inline void *snb_append(struct snbuf *snb, uint16_t len) {
  return rte_pktmbuf_append(&snb->mbuf, len);
}

/* remove bytes from the end */
static inline void snb_trim(struct snbuf *snb, uint16_t to_remove) {
  int ret;

  ret = rte_pktmbuf_trim(&snb->mbuf, to_remove);
  DCHECK_EQ(ret, 0);
}

static inline struct snbuf *snb_copy(struct snbuf *src) {
  struct snbuf *dst;

  DCHECK(snb_is_linear(src));

  dst = __snb_alloc_pool(src->mbuf.pool);

  rte_memcpy(snb_append(dst, snb_total_len(src)), snb_head_data(src),
             snb_total_len(src));

  return dst;
}

static inline phys_addr_t snb_seg_dma_addr(struct rte_mbuf *mbuf) {
  return mbuf->buf_physaddr + mbuf->data_off;
}

static inline phys_addr_t snb_dma_addr(struct snbuf *snb) {
  return snb_seg_dma_addr(&snb->mbuf);
}

struct rte_mempool *get_pframe_pool();
struct rte_mempool *get_pframe_pool_socket(int socket);

static inline phys_addr_t snb_to_paddr(struct snbuf *snb) {
  return snb->paddr;
}

static inline int mt_offset_to_databuf_offset(
    bess::metadata::mt_offset_t offset) {
  return offset + offsetof(struct snbuf, _metadata) -
         offsetof(struct snbuf, _headroom);
}

/* Slow. Do not use in the datapath */
struct snbuf *paddr_to_snb(phys_addr_t paddr);

void snb_dump(FILE *file, struct snbuf *pkt);

void init_mempool(void);
void close_mempool(void);

#endif  // BESS_SNBUF_H_
