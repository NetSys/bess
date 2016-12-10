#ifndef BESS_SNBUF_AVX_H_
#define BESS_SNBUF_AVX_H_

#ifndef BESS_SNBUF_H_
#error "Do not directly include this file. Include snbuf.h instead."
#endif

#include "utils/simd.h"

static inline int snb_alloc_bulk(snb_array_t snbs, int cnt, uint16_t len) {
  int ret;
  int i;

  __m128i mbuf_template; /* 256-bit write was worse... */
  __m128i rxdesc_fields;

  /* DPDK 2.1 or higher
   * packet_type		0 	(32 bits)
   * pkt_len 		len	(32 bits)
   * data_len 		len 	(16 bits)
   * vlan_tci 		0 	(16 bits)
   * rss 			0 	(32 bits) */
  rxdesc_fields = _mm_setr_epi32(0, len, len, 0);

  ret = rte_mempool_get_bulk(ctx.pframe_pool(), reinterpret_cast<void **>(snbs),
                             cnt);
  if (ret != 0) {
    return 0;
  }

  mbuf_template = *(reinterpret_cast<__m128i *>(&pframe_template.buf_len));

  /* 4 at a time didn't help */
  for (i = 0; i < (cnt & (~0x1)); i += 2) {
    /* since the data is likely to be in the store buffer
     * as 64-bit writes, 128-bit read will cause stalls */
    struct snbuf *snb0 = snbs[i];
    struct snbuf *snb1 = snbs[i + 1];

    _mm_storeu_si128(reinterpret_cast<__m128i *>(&snb0->mbuf.buf_len),
                     mbuf_template);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(&snb0->mbuf.packet_type),
                     rxdesc_fields);

    _mm_storeu_si128(reinterpret_cast<__m128i *>(&snb1->mbuf.buf_len),
                     mbuf_template);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(&snb1->mbuf.packet_type),
                     rxdesc_fields);
  }

  if (cnt & 0x1) {
    struct snbuf *snb = snbs[i];

    _mm_storeu_si128(reinterpret_cast<__m128i *>(&snb->mbuf.buf_len),
                     mbuf_template);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(&snb->mbuf.packet_type),
                     rxdesc_fields);
  }

  return cnt;
}

/* for packets to be processed in the fast path, all packets must:
 * 1. share the same mempool
 * 2. single segment
 * 3. reference counter == 1
 * 4. the data buffer is embedded in the mbuf
 *    (Do not use RTE_MBUF_(IN)DIRECT, since there is a difference
 *     between DPDK 1.8 and 2.0) */
static inline void snb_free_bulk(snb_array_t snbs, int cnt) {
  struct rte_mempool *_pool = snbs[0]->mbuf.pool;

  /* broadcast */
  __m128i offset = _mm_set1_epi64x(SNBUF_HEADROOM_OFF);
  __m128i info_mask = _mm_set1_epi64x(0x00ffffff00000000UL);
  __m128i info_simple = _mm_set1_epi64x(0x0001000100000000UL);
  __m128i pool = _mm_set1_epi64x((uintptr_t)_pool);

  int i;

  for (i = 0; i < (cnt & ~1); i += 2) {
    struct rte_mbuf *mbuf0 = (struct rte_mbuf *)snbs[i];
    struct rte_mbuf *mbuf1 = (struct rte_mbuf *)snbs[i + 1];

    __m128i buf_addrs_derived;
    __m128i buf_addrs_actual;
    __m128i info;
    __m128i pools;
    __m128i vcmp1, vcmp2, vcmp3;

    __m128i mbuf_ptrs = _mm_set_epi64x((uintptr_t)mbuf1, (uintptr_t)mbuf0);

    buf_addrs_actual = gather_m128i(&mbuf0->buf_addr, &mbuf1->buf_addr);
    buf_addrs_derived = _mm_add_epi64(mbuf_ptrs, offset);

    /* refcnt and nb_segs must be 1 */
    info = gather_m128i(&mbuf0->buf_len, &mbuf1->buf_len);
    info = _mm_and_si128(info, info_mask);

    pools = gather_m128i(&mbuf0->pool, &mbuf1->pool);

    vcmp1 = _mm_cmpeq_epi64(buf_addrs_derived, buf_addrs_actual);
    vcmp2 = _mm_cmpeq_epi64(info, info_simple);
    vcmp3 = _mm_cmpeq_epi64(pool, pools);

    vcmp1 = _mm_and_si128(vcmp1, vcmp2);
    vcmp1 = _mm_and_si128(vcmp1, vcmp3);

    if (unlikely(_mm_movemask_epi8(vcmp1) != 0xffff)) {
      goto slow_path;
    }
  }

  if (i < cnt) {
    struct snbuf *snb = snbs[i];

    if (unlikely(snb->mbuf.pool != _pool || snb->mbuf.next != nullptr ||
                 rte_mbuf_refcnt_read(&snb->mbuf) != 1 ||
                 snb->mbuf.buf_addr != snb->_headroom)) {
      goto slow_path;
    }
  }

  /* NOTE: it seems that zeroing the refcnt of mbufs is not necessary.
   *   (allocators will reset them) */
  rte_mempool_put_bulk(_pool, reinterpret_cast<void **>(snbs), cnt);
  return;

slow_path:
  for (i = 0; i < cnt; i++) {
    snb_free(snbs[i]);
  }
}

#endif  // BESS_SNBUF_AVX_H_
