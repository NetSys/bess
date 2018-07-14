// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_PACKET_AVX_H_
#define BESS_PACKET_AVX_H_

#ifndef BESS_PACKET_H_
#error "Do not directly include this file. Include packet.h instead."
#endif

#include <glog/logging.h>

#include "utils/simd.h"

// for packets to be processed in the fast path, all packets must:
// 1. share the same mempool
// 2. single segment
// 3. reference counter == 1
// 4. the data buffer is embedded in the mbuf
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
    info = gather_m128i(&mbuf0->rearm_data_, &mbuf1->rearm_data_);
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
