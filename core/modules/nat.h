#ifndef BESS_MODULES_NAT_H_
#define BESS_MODULES_NAT_H_

#include "../module.h"
#include "../module_msg.pb.h"

#include <rte_config.h>
#include <rte_hash_crc.h>

#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../utils/cuckoo_map.h"
#include "../utils/endian.h"
#include "../utils/random.h"

// Theory of operation:
//
// Definitions:
// Endpoint = <IPv4 address, port>
// Forward direction = outbound (internal -> external)
// Reverse direction = inbound (external -> internal)
//
// There is a single hash table of Endpoint -> (Endpoint, timestamp), which
// contains both forward and reverse mapping. They have the same lifespan.
// (e.g., if one entry is deleted, its peer is also deleted)
//
// Suppose the table is empty, and we see a packet A:a ===> B:b.
// Then we find a free external endpoint A':a' for the internal endpoint A:a
// from the pool and create two entries:
// - entry 1  A:a -> A':a'
// - entry 2  A':a' -> A:a
// Then the packet is updated to A':a' ===> B:b (with entry 1).
// When a return packet B:b ===> A':a' comes in, the destination (since it is
// reverse dir) endpoint is B:b ===> A:a (with entry 2).

using bess::utils::be16_t;
using bess::utils::be32_t;

struct alignas(8) Endpoint {
  be32_t addr;

  // TCP/UDP port number or ICMP identifier
  be16_t port;

  // L4 protocol (IPPROTO_*). Note that this is a 1-byte field in the IP header,
  // but we store the value in a 2-byte field so that the struct be 8-byte long
  // without a hole, without needing to initialize it explicitly.
  uint16_t protocol;

  struct Hash {
    std::size_t operator()(const Endpoint &e) const {
#if __SSE4_2__ && __x86_64
      return crc32c_sse42_u64(
          (static_cast<uint64_t>(e.addr.raw_value()) << 32) |
              (static_cast<uint64_t>(e.port.raw_value()) << 16) |
              static_cast<uint64_t>(e.protocol),
          0);
#else
      return rte_hash_crc(&e, sizeof(uint64_t), 0);
#endif
    }
  };

  struct EqualTo {
    bool operator()(const Endpoint &lhs, const Endpoint &rhs) const {
      const union {
        Endpoint endpoint;
        uint64_t u64;
      } &left = {.endpoint = lhs}, &right = {.endpoint = rhs};

      return left.u64 == right.u64;
    }
  };
};

static_assert(sizeof(Endpoint) == sizeof(uint64_t), "Incorrect Endpoint");

struct NatEntry {
  Endpoint endpoint;

  // last_refresh is only updated for forward-direction (outbound) packets, as
  // per rfc4787 REQ-6. Reverse entries will have an garbage value.
  // We do lazy reclaim of expired; NAT mapping entry will NOT expire unless it
  // runs out of ports in the pool.
  uint64_t last_refresh;  // in nanoseconds (ctx.current_ns)
};

// NAT module. 2 igates and 2 ogates
// igate/ogate 0: forward dir
// igate/ogate 1: reverse dir
class NAT final : public Module {
 public:
  enum Direction {
    kForward = 0,  // internal -> external
    kReverse = 1,  // external -> internal
  };

  static const gate_idx_t kNumIGates = 2;
  static const gate_idx_t kNumOGates = 2;

  CommandResponse Init(const bess::pb::NATArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  // returns the number of active NAT entries (flows)
  std::string GetDesc() const override;

 private:
  using HashTable = bess::utils::CuckooMap<Endpoint, NatEntry, Endpoint::Hash,
                                           Endpoint::EqualTo>;

  // 5 minutes for entry expiration (rfc4787 REQ-5-c)
  static const uint64_t kTimeOutNs = 300ull * 1000 * 1000 * 1000;

  // how many times shall we try to find a free port number?
  static const int kMaxTrials = 128;

  HashTable::Entry *CreateNewEntry(const Endpoint &internal, uint64_t now);

  template <Direction dir>
  void DoProcessBatch(bess::PacketBatch *batch);

  std::vector<be32_t> ext_addrs_;

  HashTable map_;
  Random rng_;
};

#endif  // BESS_MODULES_NAT_H_
