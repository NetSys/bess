#ifndef BESS_MODULES_NAT_H_
#define BESS_MODULES_NAT_H_

#include <arpa/inet.h>
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

using bess::utils::IPAddress;
using bess::utils::CIDRNetwork;
using bess::utils::HashResult;
using bess::utils::CuckooMap;

const uint16_t MIN_PORT = 1024;
const uint16_t MAX_PORT = 65535;
const uint64_t TIME_OUT_NS = (uint64_t)120 * 1000 * 1000 * 1000;

enum Protocol : uint8_t {
  ICMP = 0x01,
  TCP = 0x06,
  UDP = 0x11,
};

// 5 tuple for TCP/UDP packets with an additional icmp_ident for ICMP query pkts
class alignas(16) Flow {
 public:
  union {
    struct {
      IPAddress src_ip;
      IPAddress dst_ip;
      union {
        uint16_t src_port;
        uint16_t icmp_ident;  // identifier of ICMP query
      };
      uint16_t dst_port;
      uint8_t proto;
    };

    struct {
      uint64_t e1;
      uint64_t e2;
    };
  };

  Flow() : e1(0), e2(0) {}

  Flow(uint32_t sip, uint32_t dip, uint16_t sp = 0, uint16_t dp = 0,
       uint8_t protocol = 0)
      : src_ip(sip), dst_ip(dip) {
    e2 = 0;
    src_port = sp;
    dst_port = dp;
    proto = protocol;
  }

  // Returns a new instance of reserse flow
  Flow ReverseFlow() const {
    if (proto == ICMP) {
      return Flow(dst_ip, src_ip, icmp_ident, 0, ICMP);
    } else {
      return Flow(dst_ip, src_ip, dst_port, src_port, proto);
    }
  }

  bool operator==(const Flow &other) const {
    return e1 == other.e1 && e2 == other.e2;
  }

  std::string ToString() const;
};

static_assert(sizeof(Flow) == 2 * sizeof(uint64_t), "Flow must be 16 bytes.");

// Stores flow information
class FlowRecord {
 public:
  Flow internal_flow;
  Flow external_flow;
  uint64_t time;
  uint16_t port;

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
    min_ = ntohl(prefix_.addr & prefix_.mask);
    max_ = ntohl(prefix_.addr | (~prefix_.mask));

    for (uint32_t ip = min_; ip <= max_; ip++) {
      for (uint32_t port = MIN_PORT; port <= MAX_PORT; port++) {
        records_.emplace_back();
        free_list_.emplace_back(ip, port);
      }
    }
    std::random_shuffle(free_list_.begin(), free_list_.end());
  }

  // Returns a random free IP/port pair within the network and removes it from
  // the free list.
  std::tuple<IPAddress, uint16_t, FlowRecord *> RandomFreeIPAndPort() {
    uint32_t ip;
    uint16_t port;

    std::tie(ip, port) = free_list_.back();
    free_list_.pop_back();

    size_t index = (port - MIN_PORT) + (ip - min_) * (MAX_PORT - MIN_PORT + 1);
    FlowRecord *record = &records_[index];

    return std::make_tuple(htonl(ip), htons(port), record);
  }

  // Adds the index of given IP/port pair back to the free list.
  void FreeAllocated(const std::tuple<IPAddress, uint16_t, FlowRecord *> &a) {
    uint32_t ip = ntohl(std::get<0>(a));
    uint16_t port = ntohs(std::get<1>(a));
    free_list_.emplace_back(ip, port);
  }

  // Returns true if there are no free remaining IP/port pairs.
  bool empty() const { return free_list_.empty(); }

  const CIDRNetwork &prefix() const { return prefix_; }

  uint64_t next_expiry() const { return next_expiry_; }

  void set_next_expiry(uint64_t next_expiry) { next_expiry_ = next_expiry; }

 private:
  CIDRNetwork prefix_;
  std::vector<FlowRecord> records_;
  std::vector<std::pair<uint32_t, uint16_t>> free_list_;
  uint64_t next_expiry_;
  uint32_t min_, max_;
};

class FlowHash {
 public:
  HashResult operator()(const Flow &key) const {
    HashResult init_val = 0;
#if __SSE4_2__ && __x86_64
    init_val = crc32c_sse42_u64(key.e1, init_val);
    init_val = crc32c_sse42_u64(key.e2, init_val);
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

  pb_error_t Init(const bess::pb::NATArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  pb_cmd_response_t CommandAdd(const bess::pb::NATArg &arg);
  pb_cmd_response_t CommandClear(const bess::pb::EmptyArg &arg);

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

#endif  // BESS_MODULES_NAT_H_
