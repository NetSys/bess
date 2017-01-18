#ifndef BESS_MODULES_NAT_H_
#define BESS_MODULES_NAT_H_

#include <arpa/inet.h>

#include <map>
#include <utility>
#include <vector>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/ip.h"
#include "../utils/random.h"

using bess::utils::IPAddress;
using bess::utils::CIDRNetwork;

typedef std::pair<CIDRNetwork, CIDRNetwork> NATRule;

// 5 tuple for TCP/UDP packets with an additional icmp_ident for ICMP query pkts
class Flow {
 public:
  IPAddress src_ip;
  IPAddress dst_ip;
  uint16_t src_port;
  uint16_t dst_port;
  uint16_t icmp_ident;  // identifier of ICMP query
  uint8_t proto;

  // Returns a new instance of reserse flow
  Flow ReverseFlow() const {
    return {.src_ip = dst_ip,
            .dst_ip = src_ip,
            .src_port = dst_port,
            .dst_port = src_port,
            .icmp_ident = icmp_ident,
            .proto = proto};
  }

  bool operator==(const Flow &other) const {
    return proto == other.proto && src_ip == other.src_ip &&
           src_port == other.src_port && dst_ip == other.dst_ip &&
           dst_port == other.dst_port && icmp_ident == other.icmp_ident;
  }
};

struct FlowHash {
  std::size_t operator()(const Flow &f) const {
    static_assert(sizeof(Flow) == 2 * sizeof(uint64_t), "Flow must be 16 bytes.");
    const uint64_t *flowdata = reinterpret_cast<const uint64_t *>(&f);
    uint64_t flowdata_xor = *flowdata ^ *(flowdata + 1);
    return std::hash<uint64_t>{}(flowdata_xor);
  }
};

// Stores flow information
class FlowRecord {
 public:
  uint16_t port;
  Flow internal_flow;
  Flow external_flow;
  uint64_t time;

  FlowRecord() : port(0), time(0) {}
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
  IPAddress RandomIP(const CIDRNetwork &net) {
    uint32_t min = ntohl(net.addr & net.mask);
    uint32_t max = ntohl(net.addr | (~net.mask));
    return htonl(rng_.GetRange(max - min) + min);
  }

  std::vector<NATRule> rules_;
  // TODO(clan): Do not share ports across entire NAT
  std::vector<uint16_t> available_ports_;
  std::unordered_map<Flow, FlowRecord &, FlowHash> flow_hash_;
  std::vector<FlowRecord> flow_vec_;
  uint64_t next_expiry_;
  Random rng_;
};

#endif  // BESS_MODULES_NAT_H_
