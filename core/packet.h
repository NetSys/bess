#ifndef BESS_PACKET_H_
#define BESS_PACKET_H_

#include <cassert>
#include <type_traits>

#include <rte_config.h>
#include <rte_mbuf.h>

#include "metadata.h"
#include "worker.h"

#include "snbuf_layout.h"

/* NOTE: NEVER use rte_pktmbuf_*() directly,
 *       unless you know what you are doing */
static_assert(SNBUF_MBUF == sizeof(struct rte_mbuf),
              "DPDK compatibility check failed");
static_assert(SNBUF_HEADROOM == RTE_PKTMBUF_HEADROOM,
              "DPDK compatibility check failed");

namespace bess {

// should probably be a static member of Packet
inline phys_addr_t seg_dma_addr(struct rte_mbuf *mbuf) {
  return mbuf->buf_physaddr + mbuf->data_off;
}

class Packet;
typedef Packet **PacketArray;

static inline Packet *__packet_alloc_pool(struct rte_mempool *pool) {
  struct rte_mbuf *mbuf;

  mbuf = rte_pktmbuf_alloc(pool);

  return reinterpret_cast<Packet *>(mbuf);
}

static inline Packet *__packet_alloc() {
  return reinterpret_cast<Packet *>(rte_pktmbuf_alloc(ctx.pframe_pool()));
}

struct rte_mempool *get_pframe_pool();
struct rte_mempool *get_pframe_pool_socket(int socket);

void init_mempool(void);
void close_mempool(void);

// For the layout of snbuf, see snbuf_layout.h
class Packet {
 public:
  struct rte_mbuf &mbuf() {
    return mbuf_;
  }
  void set_mbuf_data_off(uint16_t offset) { mbuf_.data_off = offset; }
  void set_mbuf_data_len(uint16_t len) { mbuf_.data_len = len; }
  void set_mbuf_pkt_len(uint32_t len) { mbuf_.pkt_len = len; }

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
  T head_data() {
    return rte_pktmbuf_mtod(&mbuf_, T);
  }

  template <typename T = char *>
  T data() {
    return reinterpret_cast<T>(data_);
  }

  template <typename T = char *>
  T metadata() {
    return reinterpret_cast<T>(metadata_);
  }

  template <typename T = char *>
  T scratchpad() {
    return reinterpret_cast<T>(scratchpad_);
  }

  int head_len() const { return rte_pktmbuf_data_len(&mbuf_); }

  int total_len() const { return rte_pktmbuf_pkt_len(&mbuf_); }

  // single segment?
  int is_linear() const { return rte_pktmbuf_is_contiguous(&mbuf_); }

  // single segment and direct?
  int is_simple() const { return is_linear() && RTE_MBUF_DIRECT(&mbuf_); }

  void *prepend(uint16_t len) {
    if (unlikely(mbuf_.data_off < len))
      return nullptr;

    mbuf_.data_off -= len;
    mbuf_.data_len += len;
    mbuf_.pkt_len += len;

    return head_data();
  }

  // remove bytes from the beginning
  void *adj(uint16_t len) {
    if (unlikely(mbuf_.data_len < len))
      return nullptr;

    mbuf_.data_off += len;
    mbuf_.data_len -= len;
    mbuf_.pkt_len -= len;

    return head_data();
  }

  // add bytes to the end
  void *append(uint16_t len) { return rte_pktmbuf_append(&mbuf_, len); }

  // remove bytes from the end
  void trim(uint16_t to_remove) {
    int ret;

    ret = rte_pktmbuf_trim(&mbuf_, to_remove);
    assert(ret == 0);
  }

  Packet *copy(Packet *src) {
    Packet *dst;

    assert(src->is_linear());

    dst = __packet_alloc_pool(src->mbuf_.pool);

    rte_memcpy(dst->append(src->total_len()), src->head_data(),
               src->total_len());

    return dst;
  }

  phys_addr_t dma_addr() { return seg_dma_addr(&mbuf_); }

  // TODO: stream operator

  std::string Dump();

  static Packet *from_paddr(phys_addr_t paddr);

  static int mt_offset_to_databuf_offset(bess::metadata::mt_offset_t offset) {
    return offset + offsetof(Packet, metadata_) - offsetof(Packet, headroom_);
  }

  static Packet *Alloc() {
    Packet *pkt = __packet_alloc();

    return pkt;
  }
  static inline int Alloc(PacketArray pkts, int cnt, uint16_t len);

  static void Free(Packet *pkt) {
    rte_pktmbuf_free(reinterpret_cast<struct rte_mbuf *>(pkt));
  }
  static inline void Free(PacketArray pkts, int cnt);
  static void Free(PacketBatch *batch) { Free(batch->pkts(), batch->cnt()); }

 private:
  union {
    struct rte_mbuf mbuf_;
    char _mbuf_[SNBUF_MBUF];
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

static_assert(std::is_pod<Packet>::value, "Packet is not a POD Type");

extern struct rte_mbuf pframe_template;

#if __AVX__
#include "packet_avx.h"
#else
int Packet::Alloc(PacketArray pkts, int cnt, uint16_t len) {
  int ret;
  int i;

  ret = rte_mempool_get_bulk(ctx.pframe_pool(), (void **)pkts, cnt);
  if (ret != 0)
    return 0;

  for (i = 0; i < cnt; i++) {
    Packet *pkt = pkts[i];

    rte_mbuf_refcnt_set(&pkt->mbuf_, 1);
    rte_pktmbuf_reset(&pkt->mbuf_);

    pkt->mbuf.pkt_len = pkt->mbuf_.data_len = len;
  }

  return cnt;
}

void Packet::Free(PacketArray pkts, int cnt) {
  struct rte_mempool *pool = pkts[0]->mbuf_.pool;

  int i;

  for (i = 0; i < cnt; i++) {
    struct rte_mbuf *mbuf = &pkts[i]->mbuf_;

    if (unlikely(mbuf->pool != pool || !snb_is_simple(pkts[i]) ||
                 rte_mbuf_refcnt_read(mbuf) != 1)) {
      goto slow_path;
    }
  }

  /* NOTE: it seems that zeroing the refcnt of mbufs is not necessary.
   *   (allocators will reset them) */
  rte_mempool_put_bulk(pool, (void **)pkts, cnt);
  return;

slow_path:
  for (i = 0; i < cnt; i++)
    Free(pkts[i]);
}
#endif

}  // namespace bess

#endif  // BESS_PACKET_H_
