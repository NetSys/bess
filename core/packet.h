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

#ifndef BESS_PACKET_H_
#define BESS_PACKET_H_

#include <rte_atomic.h>
#include <rte_config.h>
#include <rte_mbuf.h>

#include <algorithm>
#include <cassert>
#include <string>
#include <type_traits>

#include "mem_alloc.h"
#include "metadata.h"
#include "snbuf_layout.h"
#include "worker.h"

/* NOTE: NEVER use rte_pktmbuf_*() directly,
 *       unless you know what you are doing */
static_assert(SNBUF_MBUF == sizeof(struct rte_mbuf),
              "DPDK compatibility check failed");
static_assert(SNBUF_HEADROOM == RTE_PKTMBUF_HEADROOM,
              "DPDK compatibility check failed");

static_assert(SNBUF_IMMUTABLE_OFF == 128,
              "Packet immbutable offset must be 128");
static_assert(SNBUF_METADATA_OFF == 192, "Packet metadata offset must by 192");
static_assert(SNBUF_SCRATCHPAD_OFF == 320,
              "Packet scratchpad offset must be 320");

namespace bess {

class Packet;

static inline Packet *__packet_alloc_pool(struct rte_mempool *pool) {
  struct rte_mbuf *mbuf;

  mbuf = rte_pktmbuf_alloc(pool);

  return reinterpret_cast<Packet *>(mbuf);
}

static inline Packet *__packet_alloc() {
  return reinterpret_cast<Packet *>(
      rte_pktmbuf_alloc(current_worker.pframe_pool()));
}

struct rte_mempool *get_pframe_pool_socket(int socket);

void init_mempool(void);
void close_mempool(void);

// For the layout of snbuf, see snbuf_layout.h
class alignas(64) Packet {
 public:
  Packet();

  // The default new operator does not honor the 64B alignment requirement of
  // this class, since it is larger than max_align_t (16B)
  static void *operator new(size_t size) {
    return mem_alloc_ex(size, alignof(Packet), 0);
  }

  static void operator delete(void *ptr) { mem_free(ptr); }

  struct rte_mbuf &as_rte_mbuf() {
    return *reinterpret_cast<struct rte_mbuf *>(this);
  }

  const struct rte_mbuf &as_rte_mbuf() const {
    return *reinterpret_cast<const struct rte_mbuf *>(this);
  }

  Packet *vaddr() const { return vaddr_; }
  void set_vaddr(Packet *addr) { vaddr_ = addr; }

  phys_addr_t paddr() { return paddr_; }
  void set_paddr(phys_addr_t addr) { paddr_ = addr; }

  uint32_t sid() const { return sid_; }
  void set_sid(uint32_t sid) { sid_ = sid; }

  uint32_t index() const { return index_; }
  void set_index(uint32_t index) { index_ = index; }

  template <typename T = char *>
  T reserve() {
    return reinterpret_cast<T>(reserve_);
  }

  template <typename T = void *>
  const T head_data(uint16_t offset = 0) const {
    return reinterpret_cast<T>(static_cast<char *>(buf_addr_) + data_off_ +
                               offset);
  }

  template <typename T = void *>
  T head_data(uint16_t offset = 0) {
    return const_cast<T>(
        static_cast<const Packet &>(*this).head_data<T>(offset));
  }

  template <typename T = char *>
  T data() {
    return reinterpret_cast<T>(data_);
  }

  template <typename T = char *>
  T metadata() const {
    return reinterpret_cast<T>(metadata_);
  }

  template <typename T = char *>
  T scratchpad() {
    return reinterpret_cast<T>(scratchpad_);
  }

  template <typename T = void *>
  T buffer() {
    return reinterpret_cast<T>(buf_addr_);
  }
  void set_buffer(void *addr) { buf_addr_ = addr; }

  int nb_segs() const { return nb_segs_; }
  void set_nb_segs(int n) { nb_segs_ = n; }

  Packet *next() const { return next_; }
  void set_next(Packet *next) { next_ = next; }

  uint16_t data_off() { return data_off_; }
  void set_data_off(uint16_t offset) { data_off_ = offset; }

  uint16_t data_len() { return data_len_; }
  void set_data_len(uint16_t len) { data_len_ = len; }

  int head_len() const { return data_len_; }

  int total_len() const { return pkt_len_; }
  void set_total_len(uint32_t len) { pkt_len_ = len; }

  uint16_t refcnt() const { return rte_mbuf_refcnt_read(&as_rte_mbuf()); }

  void set_refcnt(uint16_t cnt) { rte_mbuf_refcnt_set(&as_rte_mbuf(), cnt); }

  // add cnt to refcnt
  void update_refcnt(uint16_t cnt) {
    rte_mbuf_refcnt_update(&as_rte_mbuf(), cnt);
  }

  uint16_t headroom() const { return rte_pktmbuf_headroom(&as_rte_mbuf()); }

  uint16_t tailroom() const { return rte_pktmbuf_tailroom(&as_rte_mbuf()); }

  // single segment?
  int is_linear() const { return rte_pktmbuf_is_contiguous(&as_rte_mbuf()); }

  // single segment and direct?
  int is_simple() const {
    return is_linear() && RTE_MBUF_DIRECT(&as_rte_mbuf());
  }

  void reset() { rte_pktmbuf_reset(&as_rte_mbuf()); }

  void *prepend(uint16_t len) {
    if (unlikely(data_off_ < len))
      return nullptr;

    data_off_ -= len;
    data_len_ += len;
    pkt_len_ += len;

    return head_data();
  }

  // remove bytes from the beginning
  void *adj(uint16_t len) {
    if (unlikely(data_len_ < len))
      return nullptr;

    data_off_ += len;
    data_len_ -= len;
    pkt_len_ -= len;

    return head_data();
  }

  // add bytes to the end
  void *append(uint16_t len) { return rte_pktmbuf_append(&as_rte_mbuf(), len); }

  // remove bytes from the end
  void trim(uint16_t to_remove) {
    int ret;

    ret = rte_pktmbuf_trim(&as_rte_mbuf(), to_remove);
    DCHECK_EQ(ret, 0);
  }

  // returns nullptr if memory allocation failed
  static Packet *copy(const Packet *src) {
    Packet *dst;

    DCHECK(src->is_linear());

    dst = __packet_alloc_pool(src->pool_);
    if (!dst) {
      return nullptr;  // FAIL.
    }

    bess::utils::CopyInlined(dst->append(src->total_len()), src->head_data(),
                             src->total_len(), true);

    return dst;
  }

  phys_addr_t dma_addr() { return buf_physaddr_ + data_off_; }

  std::string Dump();

  static Packet *from_paddr(phys_addr_t paddr);

  static int mt_offset_to_databuf_offset(bess::metadata::mt_offset_t offset) {
    return offset + offsetof(Packet, metadata_) - offsetof(Packet, headroom_);
  }

  static Packet *Alloc() { return __packet_alloc(); }

  // cnt must be [0, PacketBatch::kMaxBurst]
  static inline size_t Alloc(Packet **pkts, size_t cnt, uint16_t len);

  // pkt may be nullptr
  static void Free(Packet *pkt) {
    rte_pktmbuf_free(reinterpret_cast<struct rte_mbuf *>(pkt));
  }

  // All pointers in pkts must not be nullptr.
  // cnt must be [0, PacketBatch::kMaxBurst]
  static inline void Free(Packet **pkts, size_t cnt);

  // batch must not be nullptr
  static void Free(PacketBatch *batch) { Free(batch->pkts(), batch->cnt()); }

 private:
  union {
    struct {
      // offset 0: Virtual address of segment buffer.
      void *buf_addr_;
      // offset 8: Physical address of segment buffer.
      alignas(8) phys_addr_t buf_physaddr_;

      union {
        __m128i rearm_data_;

        struct {
          // offset 16:
          alignas(8) uint16_t data_off_;

          // offset 18:
          uint16_t refcnt_;

          // offset 20:
          uint16_t nb_segs_;  // Number of segments

          // offset 22:
          uint16_t _dummy0_;  // rte_mbuf.port
          // offset 24:
          uint64_t _dummy1_;  // rte_mbuf.ol_flags
        };
      };

      union {
        __m128i rx_descriptor_fields1_;

        struct {
          // offset 32:
          uint32_t _dummy2_;  // rte_mbuf.packet_type_;

          // offset 36:
          uint32_t pkt_len_;  // Total pkt length: sum of all segments

          // offset 40:
          uint16_t data_len_;  // Amount of data in this segment

          // offset 42:
          uint16_t _dummy3_;  // rte_mbuf.vlan_tci

          // offset 44:
          uint32_t _dummy4_lo;  // rte_mbuf.fdir.lo and rte_mbuf.rss
        };
      };

      // offset 48:
      uint32_t _dummy4_hi;  // rte_mbuf.fdir.hi

      // offset 52:
      uint16_t _dummy5_;  // rte_mbuf.vlan_tci_outer

      // offset 54:
      const uint16_t buf_len_;

      // offset 56:
      uint64_t _dummy6_;  // rte_mbuf.timestamp

      // 2nd cacheline - fields only used in slow path or on TX --------------
      // offset 64:
      uint64_t _dummy7_;  // rte_mbuf.userdata

      // offset 72:
      struct rte_mempool *pool_;  // Pool from which mbuf was allocated.

      // offset 80:
      Packet *next_;  // Next segment. nullptr if not scattered.

      // offset 88:
      uint64_t _dummy8;   // rte_mbuf.tx_offload
      uint16_t _dummy9;   // rte_mbuf.priv_size
      uint16_t _dummy10;  // rte_mbuf.timesync
      uint32_t _dummy11;  // rte_mbuf.seqn

      // offset 104:
    };

    struct rte_mbuf mbuf_;
  };

  union {
    char reserve_[SNBUF_RESERVE];

    struct {
      union {
        char immutable_[SNBUF_IMMUTABLE];

        const struct {
          // must be the first field
          Packet *vaddr_;

          phys_addr_t paddr_;

          // socket ID
          uint32_t sid_;

          // packet index within the pool
          uint32_t index_;
        };
      };

      // Dynamic metadata.
      // Each attribute value is stored in host order
      char metadata_[SNBUF_METADATA];

      // Used for module/driver-specific data
      char scratchpad_[SNBUF_SCRATCHPAD];
    };
  };

  char headroom_[SNBUF_HEADROOM];
  char data_[SNBUF_DATA];
};

static_assert(std::is_standard_layout<Packet>::value, "Incorrect class Packet");
static_assert(sizeof(Packet) == SNBUF_SIZE, "Incorrect class Packet");

#if __AVX__
#include "packet_avx.h"
#else
inline size_t Packet::Alloc(Packet **pkts, size_t cnt, uint16_t len) {
  DCHECK_LE(cnt, PacketBatch::kMaxBurst);

  // rte_mempool_get_bulk() is all (cnt) or nothing (0)
  if (rte_mempool_get_bulk(current_worker.pframe_pool(),
                           reinterpret_cast<void **>(pkts), cnt) < 0) {
    return 0;
  }

  for (size_t i = 0; i < cnt; i++) {
    Packet *pkt = pkts[i];

    pkt->set_refcnt(1);
    pkt->reset();
    pkt->pkt_len_ = pkt->data_len_ = len;
  }

  return cnt;
}

inline void Packet::Free(Packet **pkts, size_t cnt) {
  DCHECK_LE(cnt, PacketBatch::kMaxBurst);

  // rte_mempool_put_bulk() crashes when called with cnt == 0
  if (unlikely(cnt <= 0)) {
    return;
  }

  struct rte_mempool *pool = pkts[0]->pool_;

  for (size_t i = 0; i < cnt; i++) {
    const Packet *pkt = pkts[i];

    if (unlikely(pkt->pool_ != pool || !pkt->is_simple() ||
                 pkt->refcnt() != 1)) {
      goto slow_path;
    }
  }

  /* NOTE: it seems that zeroing the refcnt of mbufs is not necessary.
   *   (allocators will reset them) */
  rte_mempool_put_bulk(pool, reinterpret_cast<void **>(pkts), cnt);
  return;

slow_path:
  // slow path: packets are not homogeneous or simple enough
  for (size_t i = 0; i < cnt; i++) {
    Free(pkts[i]);
  }
}
#endif

}  // namespace bess

#endif  // BESS_PACKET_H_
