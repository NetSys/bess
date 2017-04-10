#ifndef BESS_PACKET_H_
#define BESS_PACKET_H_

#include <rte_atomic.h>
#include <rte_config.h>
#include <rte_mbuf.h>

#include <algorithm>
#include <cassert>
#include <string>
#include <type_traits>

#include "metadata.h"
#include "worker.h"

#include "snbuf_layout.h"

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
  return reinterpret_cast<Packet *>(rte_pktmbuf_alloc(ctx.pframe_pool()));
}

struct rte_mempool *get_pframe_pool();
struct rte_mempool *get_pframe_pool_socket(int socket);

void init_mempool(void);
void close_mempool(void);

// For the layout of snbuf, see snbuf_layout.h
class alignas(64) Packet {
 public:
  // TODO: delete constructor/destructor w/o breaking module_bench

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
    return const_cast<T>(static_cast<const Packet &>(*this).head_data<T>(offset));
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

  static Packet *copy(const Packet *src) {
    Packet *dst;

    DCHECK(src->is_linear());

    dst = __packet_alloc_pool(src->pool_);

    if(!dst){
      return nullptr; //FAIL.
    }

    rte_memcpy(dst->append(src->total_len()), src->head_data(),
               src->total_len());

    return dst;
  }

  phys_addr_t dma_addr() { return buf_physaddr_ + data_off_; }

  std::string Dump();

  static Packet *from_paddr(phys_addr_t paddr);

  static int mt_offset_to_databuf_offset(bess::metadata::mt_offset_t offset) {
    return offset + offsetof(Packet, metadata_) - offsetof(Packet, headroom_);
  }

  static Packet *Alloc() {
    return __packet_alloc();
  }

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
  typedef void *MARKER[0];     // generic marker for a point in a structure
  typedef uint8_t MARKER8[0];  // generic marker with 1B alignment

  union {
    // This is all lifted from rte_mbuf.h
    struct {
      MARKER cacheline0_;

      void *buf_addr_;            // Virtual address of segment buffer.
      phys_addr_t buf_physaddr_;  // Physical address of segment buffer.

      uint16_t buf_len_;  // Length of segment buffer.

      // next 6 bytes are initialised on RX descriptor rearm
      MARKER8 rearm_data_;
      uint16_t data_off_;

      /**
       * 16-bit Reference counter.
       * It should only be accessed using the following functions:
       * rte_mbuf_refcnt_update(), rte_mbuf_refcnt_read(), and
       * rte_mbuf_refcnt_set(). The functionality of these functions (atomic,
       * or non-atomic) is controlled by the CONFIG_RTE_MBUF_REFCNT_ATOMIC
       * config option.
       */
      union {
        rte_atomic16_t refcnt_atomic_;  // Atomically accessed refcnt
        uint16_t refcnt_;               // Non-atomically accessed refcnt
      };
      uint8_t nb_segs_;  // Number of segments.
      uint8_t port_;     // Input port.

      uint64_t offload_flags_;  // Offload features.

      // remaining bytes are set on RX when pulling packet from descriptor
      MARKER rx_descriptor_fields1_;

      /*
       * The packet type, which is the combination of outer/inner L2, L3, L4
       * and tunnel types. The packet_type is about data really present in the
       * mbuf. Example: if vlan stripping is enabled, a received vlan packet
       * would have RTE_PTYPE_L2_ETHER and not RTE_PTYPE_L2_VLAN because the
       * vlan is stripped from the data.
       */
      union {
        uint32_t packet_type_;  // L2/L3/L4 and tunnel information.
        struct {
          uint32_t l2_type_ : 4;        // (Outer) L2 type.
          uint32_t l3_type_ : 4;        // (Outer) L3 type.
          uint32_t l4_type_ : 4;        // (Outer) L4 type.
          uint32_t tun_type_ : 4;       // Tunnel type.
          uint32_t inner_l2_type_ : 4;  // Inner L2 type.
          uint32_t inner_l3_type_ : 4;  // Inner L3 type.
          uint32_t inner_l4_type_ : 4;  // Inner L4 type.
        };
      };

      uint32_t pkt_len_;   // Total pkt len: sum of all segments.
      uint16_t data_len_;  // Amount of data in segment buffer.

      // VLAN TCI (CPU order), valid if PKT_RX_VLAN_STRIPPED is set.
      uint16_t vlan_tci_;

      union {
        uint32_t rss_;  // RSS hash result if RSS enabled
        struct {
          union {
            struct {
              uint16_t hash_;
              uint16_t id_;
            };
            uint32_t lo_;
            // Second 4 flexible bytes
          };
          uint32_t hi_;
          // First 4 flexible bytes or FD ID, dependent on
          // PKT_RX_FDIR_* flag in ol_flags.
        } fdir_;  // Filter identifier if FDIR enabled
        struct {
          uint32_t lo_;
          uint32_t hi_;
        } sched_;       // Hierarchical scheduler
        uint32_t usr_;  // User defined tags. See rte_distributor_process()
      } hash_;          // hash information

      uint32_t seqn_;  // Sequence number.

      // Outer VLAN TCI (CPU order), valid if PKT_RX_QINQ_STRIPPED is set.
      uint16_t vlan_tci_outer_;

      // second cache line - fields only used in slow path or on TX
      MARKER cacheline1_ __rte_cache_min_aligned;

      union {
        void *userdata_;    // Can be used for external metadata
        uint64_t udata64_;  // Allow 8-byte userdata on 32-bit
      };

      struct rte_mempool *pool_;  // Pool from which mbuf was allocated.
      Packet *next_;              // Next segment of scattered packet.
    };
    char mbuf_[SNBUF_MBUF];
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

extern Packet pframe_template;

#if __AVX__
#include "packet_avx.h"
#else
inline size_t Packet::Alloc(Packet **pkts, size_t cnt, uint16_t len) {
  DCHECK_LE(cnt, PacketBatch::kMaxBurst);

  // rte_mempool_get_bulk() is all (cnt) or nothing (0)
  if (rte_mempool_get_bulk(ctx.pframe_pool(), reinterpret_cast<void **>(pkts),
                           cnt) < 0) {
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
