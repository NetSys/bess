#ifndef BESS_PACKET_AVX_H_
#define BESS_PACKET_AVX_H_

#ifndef BESS_PACKET_H_
#error "Do not directly include this file. Include packet.h instead."
#endif

#include "utils/simd.h"

inline size_t Packet::Alloc(Packet **pkts, size_t cnt, uint16_t len) {
  // rte_mempool_get_bulk() is all (cnt) or nothing (0)
  if (rte_mempool_get_bulk(ctx.pframe_pool(), reinterpret_cast<void **>(pkts),
                           cnt) < 0) {
    return 0;
  }

  // DPDK 2.1 or higher:
  // packet_type     0   (32 bits)
  // pkt_len       len   (32 bits)
  // data_len      len   (16 bits)
  // vlan_tci        0   (16 bits)
  // rss             0   (32 bits)
  __m128i rxdesc_fields = _mm_setr_epi32(0, len, len, 0);
  __m128i mbuf_template =
      *(reinterpret_cast<__m128i *>(&pframe_template.buf_len_));

  size_t i;

  /* 4 at a time didn't help */
  for (i = 0; i < (cnt & (~0x1)); i += 2) {
    /* since the data is likely to be in the store buffer
     * as 64-bit writes, 128-bit read will cause stalls */
    Packet *pkt0 = pkts[i];
    Packet *pkt1 = pkts[i + 1];

    _mm_storeu_si128(reinterpret_cast<__m128i *>(&pkt0->buf_len_),
                     mbuf_template);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(&pkt0->packet_type_),
                     rxdesc_fields);

    _mm_storeu_si128(reinterpret_cast<__m128i *>(&pkt1->buf_len_),
                     mbuf_template);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(&pkt1->packet_type_),
                     rxdesc_fields);
  }

  if (cnt & 0x1) {
    Packet *pkt = pkts[i];

    _mm_storeu_si128(reinterpret_cast<__m128i *>(&pkt->buf_len_),
                     mbuf_template);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(&pkt->packet_type_),
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
inline void Packet::Free(Packet **pkts, size_t cnt) {
  DCHECK(cnt <= PacketBatch::kMaxBurst);

  // rte_mempool_put_bulk() crashes when called with cnt == 0
  if (unlikely(cnt <= 0)) {
    return;
  }

  struct rte_mempool *_pool = pkts[0]->pool_;

  // broadcast
  __m128i offset = _mm_set1_epi64x(SNBUF_HEADROOM_OFF);
  __m128i info_mask = _mm_set1_epi64x(0x00ffffff00000000UL);
  __m128i info_simple = _mm_set1_epi64x(0x0001000100000000UL);
  __m128i pool = _mm_set1_epi64x((uintptr_t)_pool);

  size_t i;

  for (i = 0; i < (cnt & ~1); i += 2) {
    auto *mbuf0 = reinterpret_cast<struct rte_mbuf *>(pkts[i]);
    auto *mbuf1 = reinterpret_cast<struct rte_mbuf *>(pkts[i + 1]);

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

    if (unlikely(_mm_movemask_epi8(vcmp1) != 0xffff))
      goto slow_path;
  }

  if (i < cnt) {
    const Packet *pkt = pkts[i];

    if (unlikely(pkt->pool_ != _pool || pkt->next_ != nullptr ||
                 rte_mbuf_refcnt_read(&pkt->as_rte_mbuf()) != 1 ||
                 pkt->buf_addr_ != pkt->headroom_)) {
      goto slow_path;
    }
  }

  /* NOTE: it seems that zeroing the refcnt of mbufs is not necessary.
   *   (allocators will reset them) */
  rte_mempool_put_bulk(_pool, reinterpret_cast<void **>(pkts), cnt);
  return;

slow_path:
  for (i = 0; i < cnt; i++) {
    Free(pkts[i]);
  }
}

#endif  // BESS_PACKET_AVX_H_
