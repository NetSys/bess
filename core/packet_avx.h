#ifndef BESS_PACKET_AVX_H_
#define BESS_PACKET_AVX_H_

#ifndef BESS_PACKET_H_
#error "Do not directly include this file. Include packet.h instead."
#endif

#include <glog/logging.h>

#include "utils/simd.h"

inline size_t Packet::Alloc(Packet **pkts, size_t cnt, uint16_t len) {
  // rte_mempool_get_bulk() is all (cnt) or nothing (0)
  if (rte_mempool_get_bulk(ctx.pframe_pool(), reinterpret_cast<void **>(pkts),
                           cnt) < 0) {
    return 0;
  }

  // We must make sure that the following 12 fields are initialized
  // as done in rte_pktmbuf_reset(). We group them into two 16-byte stores.
  //
  // - 1st store: mbuf.rearm_data
  //   2B data_off == RTE_PKTMBUF_HEADROOM (SNBUF_HEADROOM)
  //   2B refcnt == 1
  //   2B nb_segs == 1
  //   2B port == 0xffff (as of 17.05 0xff is set, but 0xffff makes more sense)
  //   8B ol_flags == 0
  //
  // - 2nd store: mbuf.rx_descriptor_fields1
  //   4B packet_type == 0
  //   4B pkt_len == len
  //   2B data_len == len
  //   2B vlan_tci == 0
  //   4B (rss == 0)       (not initialized by rte_pktmbuf_reset)
  //
  // We can ignore these fields:
  //   vlan_tci_outer == 0 (not required if ol_flags == 0)
  //   tx_offload == 0     (not required if ol_flags == 0)
  //   next == nullptr     (all packets in a mempool must already be nullptr)

  __m128i rearm = _mm_setr_epi16(SNBUF_HEADROOM, 1, 1, 0xffff, 0, 0, 0, 0);
  __m128i rxdesc = _mm_setr_epi32(0, len, len, 0);

  size_t i;

  /* 4 at a time didn't help */
  for (i = 0; i < (cnt & (~0x1)); i += 2) {
    /* since the data is likely to be in the store buffer
     * as 64-bit writes, 128-bit read will cause stalls */
    Packet *pkt0 = pkts[i];
    Packet *pkt1 = pkts[i + 1];

    _mm_store_si128(&pkt0->rearm_, rearm);
    _mm_store_si128(&pkt0->rxdesc_, rxdesc);
    _mm_store_si128(&pkt1->rearm_, rearm);
    _mm_store_si128(&pkt1->rxdesc_, rxdesc);
  }

  if (cnt & 0x1) {
    Packet *pkt = pkts[i];

    _mm_store_si128(&pkt->rearm_, rearm);
    _mm_store_si128(&pkt->rxdesc_, rxdesc);
  }

  for (i = 0; i < cnt; i++) {
    bess::Packet *pkt = pkts[i];
    DCHECK_EQ(pkt->mbuf_.data_off, RTE_PKTMBUF_HEADROOM);
    DCHECK_EQ(pkt->mbuf_.refcnt, 1);
    DCHECK_EQ(pkt->mbuf_.nb_segs, 1);
    DCHECK_EQ(pkt->mbuf_.port, 0xffff);
    DCHECK_EQ(pkt->mbuf_.ol_flags, 0);

    DCHECK_EQ(pkt->mbuf_.packet_type, 0);
    DCHECK_EQ(pkt->mbuf_.pkt_len, len);
    DCHECK_EQ(pkt->mbuf_.data_len, len);
    DCHECK_EQ(pkt->mbuf_.vlan_tci, 0);
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
  __m128i info_mask = _mm_set1_epi64x(0x0000ffffffff0000UL);
  __m128i info_simple = _mm_set1_epi64x(0x0000000100010000UL);
  __m128i pool = _mm_set1_epi64x((uintptr_t)_pool);

  size_t i;

  for (i = 0; i < (cnt & ~1); i += 2) {
    auto *mbuf0 = pkts[i];
    auto *mbuf1 = pkts[i + 1];

    __m128i buf_addrs_derived;
    __m128i buf_addrs_actual;
    __m128i info;
    __m128i pools;
    __m128i vcmp1, vcmp2, vcmp3;

    __m128i mbuf_ptrs = _mm_set_epi64x(reinterpret_cast<uintptr_t>(mbuf1),
                                       reinterpret_cast<uintptr_t>(mbuf0));

    buf_addrs_actual = gather_m128i(&mbuf0->buf_addr_, &mbuf1->buf_addr_);
    buf_addrs_derived = _mm_add_epi64(mbuf_ptrs, offset);

    /* refcnt and nb_segs must be 1 */
    info = gather_m128i(&mbuf0->rearm_, &mbuf1->rearm_);
    info = _mm_and_si128(info, info_mask);

    pools = gather_m128i(&mbuf0->pool_, &mbuf1->pool_);

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
                 pkt->refcnt_ != 1 || pkt->buf_addr_ != pkt->headroom_)) {
      goto slow_path;
    }
  }

  // When a rte_mbuf is returned to a mempool, the following conditions
  // must hold:
  for (i = 0; i < cnt; i++) {
    Packet *pkt = pkts[i];
    DCHECK_EQ(pkt->mbuf_.refcnt, 1);
    DCHECK_EQ(pkt->mbuf_.nb_segs, 1);
    DCHECK_EQ(pkt->mbuf_.next, static_cast<struct rte_mbuf *>(nullptr));
  }

  rte_mempool_put_bulk(_pool, reinterpret_cast<void **>(pkts), cnt);
  return;

slow_path:
  for (i = 0; i < cnt; i++) {
    Free(pkts[i]);
  }
}

#endif  // BESS_PACKET_AVX_H_
