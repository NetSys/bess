#ifndef BESS_MODULES_NAT_H_
#define BESS_MODULES_NAT_H_

#include <rte_config.h>
#include <rte_hash_crc.h>

#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/cuckoo_map.h"
#include "../utils/ip.h"
#include "../utils/random.h"

using bess::utils::be16_t;
using bess::utils::be32_t;
using bess::utils::CIDRNetwork;
using bess::utils::HashResult;
using bess::utils::CuckooMap;

const uint16_t MIN_PORT = 1024;
const uint16_t MAX_PORT = 65535;
const uint64_t TIME_OUT_NS = 120ull * 1000 * 1000 * 1000;

enum Protocol : uint8_t {
  ICMP = 0x01,
  TCP = 0x06,
  UDP = 0x11,
};

// 5 tuple for TCP/UDP packets with an additional icmp_ident for ICMP query pkts
class alignas(16) Flow {
 public:
  be32_t src_ip;
  be32_t dst_ip;
  union {
    be16_t src_port;
    be16_t icmp_ident;  // identifier of ICMP query
  };
  be16_t dst_port;
  uint32_t proto;  // include 24-bit padding

  Flow() {}

  Flow(be32_t sip, be32_t dip, be16_t sp = be16_t(0), be16_t dp = be16_t(0),
       uint8_t protocol = 0)
      : src_ip(sip), dst_ip(dip), src_port(sp), dst_port(dp), proto(protocol) {}

  // Returns a new instance of reserse flow
  Flow ReverseFlow() const {
    if (proto == ICMP) {
      return Flow(dst_ip, src_ip, icmp_ident, be16_t(0), ICMP);
    } else {
      return Flow(dst_ip, src_ip, dst_port, src_port, proto);
    }
  }

  bool operator==(const Flow &other) const {
    return memcmp(this, &other, sizeof(*this)) == 0;
  }

  std::string ToString() const;
};

static_assert(sizeof(Flow) == 16, "Flow must be 16 bytes.");

// Stores flow information
class FlowRecord {
 public:
  Flow internal_flow;
  Flow external_flow;
  uint64_t time;
  be16_t port;

  FlowRecord() : internal_flow(), external_flow(), time(), port() {}
};

// A data structure to track available ports for a given subnet of external IPs
// for the NAT.  Encapsulates the subnet and the mapping from IPs in that subnet
// to free ports.
class AvailablePorts {
 public:
  // Tracks available ports within the given IP prefix.
  explicit AvailablePorts(const CIDRNetwork &prefix)
      : prefix_(prefix),
        records_(),
        free_list_(),
        next_expiry_(),
        min_(),
        max_() {
    min_ = prefix_.addr.value() & prefix_.mask.value();
    max_ = prefix_.addr.value() | (~prefix_.mask.value());

    for (uint32_t ip = min_; ip <= max_; ip++) {
      for (uint32_t port = MIN_PORT; port <= MAX_PORT; port++) {
        records_.emplace_back();
        free_list_.emplace_back(be32_t(ip), be16_t((uint16_t)port));
      }
    }
    std::random_shuffle(free_list_.begin(), free_list_.end());
  }

  // Returns a random free IP/port pair within the network and removes it from
  // the free list.
  std::tuple<be32_t, be16_t, FlowRecord *> RandomFreeIPAndPort() {
    be32_t ip;
    be16_t port;

    std::tie(ip, port) = free_list_.back();
    free_list_.pop_back();

    size_t index = (port.value() - MIN_PORT) +
                   (ip.value() - min_) * (MAX_PORT - MIN_PORT + 1);
    FlowRecord *record = &records_[index];

    return std::make_tuple(ip, port, record);
  }

  // Adds the index of given IP/port pair back to the free list.
  void FreeAllocated(const std::tuple<be32_t, be16_t, FlowRecord *> &a) {
    free_list_.emplace_back(std::get<0>(a), std::get<1>(a));
  }

  // Returns true if there are no free remaining IP/port pairs.
  bool empty() const { return free_list_.empty(); }

  const CIDRNetwork &prefix() const { return prefix_; }

  uint64_t next_expiry() const { return next_expiry_; }

  void set_next_expiry(uint64_t next_expiry) { next_expiry_ = next_expiry; }

 private:
  CIDRNetwork prefix_;
  std::vector<FlowRecord> records_;
  std::vector<std::pair<be32_t, be16_t>> free_list_;
  uint64_t next_expiry_;
  uint32_t min_, max_;
};

struct FlowHash {
  std::size_t operator()(const Flow &f) const {
    const union {
      Flow flow;
      uint64_t u64[2];
    } &bytes = {.flow = f};

    uint32_t init_val = 0;
#if __SSE4_2__ && __x86_64
    init_val = crc32c_sse42_u64(bytes.u64[0], init_val);
    init_val = crc32c_sse42_u64(bytes.u64[1], init_val);
#else
    init_val = rte_hash_crc(&key.e1, sizeof(key.e1), init_val);
    init_val = rte_hash_crc(&key.e2, sizeof(key.e2), init_val);
#endif
    return init_val;
  }
};

// NAT module. 2 igates and 2 ogates
// igate/ogate 0: traffic from internal network to external network
// igate/ogate 1: traffic from external network to internal network
class NAT final : public Module {
 public:
  static const Commands cmds;
  static const gate_idx_t kNumIGates = 2;
  static const gate_idx_t kNumOGates = 2;

  CommandResponse Init(const bess::pb::NATArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const bess::pb::NATArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

 private:
  void InitRules(const bess::pb::NATArg &arg) {
    for (const auto &rule : arg.rules()) {
      CIDRNetwork int_net(rule.internal_addr_block());
      CIDRNetwork ext_net(rule.external_addr_block());
      rules_.emplace_back(std::piecewise_construct,
                          std::forward_as_tuple(int_net),
                          std::forward_as_tuple(ext_net));
    }
  }

  std::vector<std::pair<CIDRNetwork, AvailablePorts>> rules_;
  CuckooMap<Flow, FlowRecord *, FlowHash> flow_hash_;
  Random rng_;
};

#endif  // BESS_MODULES_NAT_H_(
