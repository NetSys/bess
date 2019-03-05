#ifndef BESS_PACKET_POOL_H_
#define BESS_PACKET_POOL_H_

#include "memory.h"
#include "packet.h"

// "Congiguous" here means that all packets reside in a single memory region
// in the virtual/physical address space.
//                                       Contiguous?
//                   Backed memory    Virtual  Physical  mlock()ed  fail-free
// --------------------------------------------------------------------------
// PlainPacketPool   Plain 4k pages   O        X         X          O
//   : For standalone benchmarks and unittests. Cannot be used for DMA.
//
// BessPacketPool    BESS hugepages   O        O         O          X
//   : BESS default. It allocates and manages huge pages internally.
//     No hugetlbfs is required.
//
// DpdkPacketPool    DPDK hugepages   O/X      O/X       O          O
//   : It is used as a fallback option if allocation of BessPacketPool has
//     failed. The memory region will be contiguous in most cases,
//     but not so if 2MB hugepages are used and scattered. MLX4/5 drivers
//     will fail in this case.

namespace bess {

// PacketPool is a C++ wrapper for DPDK rte_mempool. It has a pool of
// pre-populated Packet objects, which can be fetched via Alloc().
// Alloc() and Free() are thread-safe.
class PacketPool {
 public:
  static PacketPool *GetDefaultPool(int node) { return default_pools_[node]; }

  static void CreateDefaultPools(size_t capacity = kDefaultCapacity);

  // socket_id == -1 means "I don't care".
  PacketPool(size_t capacity = kDefaultCapacity, int socket_id = -1);
  virtual ~PacketPool();

  // PacketPool is neither copyable nor movable.
  PacketPool(const PacketPool &) = delete;
  PacketPool &operator=(const PacketPool &) = delete;

  // Allocate a packet from the pool, with specified initial packet size.
  Packet *Alloc(size_t len = 0) {
    Packet *pkt = reinterpret_cast<Packet *>(rte_pktmbuf_alloc(pool_));
    if (pkt) {
      pkt->pkt_len_ = len;
      pkt->data_len_ = len;

      // TODO: sanity check
    }
    return pkt;
  }

  // Allocate multiple packets. Note that this function has no partial success;
  // it allocates either all "count" packets (returns true) or none (false).
  bool AllocBulk(Packet **pkts, size_t count, size_t len = 0);

  // The number of total packets in the pool. 0 if initialization failed.
  size_t Capacity() const { return pool_->populated_size; }

  // The number of available packets in the pool. Approximate by nature.
  size_t Size() const { return rte_mempool_avail_count(pool_); }

  // Note: It would be ideal to not expose this
  rte_mempool *pool() { return pool_; }

  static Packet *from_paddr(phys_addr_t paddr);

  virtual bool IsVirtuallyContiguous() = 0;
  virtual bool IsPhysicallyContiguous() = 0;
  virtual bool IsPinned() = 0;

 protected:
  static const size_t kDefaultCapacity = (1 << 16) - 1;  // 64k - 1
  static const size_t kMaxCacheSize = 512;               // per-core cache size

  // Child classes are expected to call this function in their constructor
  void PostPopulate();

  std::string name_;
  rte_mempool *pool_;

 private:
  // Default per-node packet pools
  static PacketPool *default_pools_[RTE_MAX_NUMA_NODES];

  friend class Packet;
};

class PlainPacketPool : public PacketPool {
 public:
  PlainPacketPool(size_t capacity = kDefaultCapacity, int socket_id = -1);

  virtual bool IsVirtuallyContiguous() override { return true; }
  virtual bool IsPhysicallyContiguous() override { return false; }
  virtual bool IsPinned() override { return pinned_; }

 private:
  bool pinned_;
};

class BessPacketPool : public PacketPool {
 public:
  BessPacketPool(size_t capacity = kDefaultCapacity, int socket_id = -1);

  virtual bool IsVirtuallyContiguous() override { return true; }
  virtual bool IsPhysicallyContiguous() override { return true; }
  virtual bool IsPinned() override { return true; }

 private:
  DmaMemoryPool mem_;
};

class DpdkPacketPool : public PacketPool {
 public:
  DpdkPacketPool(size_t capacity = kDefaultCapacity, int socket_id = -1);

  // TODO(sangjin): it may or may not be contiguous. Check it.
  virtual bool IsVirtuallyContiguous() override { return false; }
  virtual bool IsPhysicallyContiguous() override { return false; }
  virtual bool IsPinned() override { return true; }
};

}  // namespace bess

#endif  // BESS_PACKET_POOL_H_
