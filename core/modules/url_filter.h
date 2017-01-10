#ifndef BESS_MODULES_URL_FILTER_H_
#define BESS_MODULES_URL_FILTER_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/tcp_flow_reconstruct.h"

using bess::utils::TcpFlowReconstruct;

template <class T>
inline void hash_combine(std::size_t &seed, const T &v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// A helper class that defines a TCP flow
class Flow {
 public:
  uint32_t src_ip;
  uint16_t src_port;
  uint32_t dst_ip;
  uint16_t dst_port;

  bool operator==(const Flow &other) const {
    return src_ip == other.src_ip && src_port == other.src_port &&
           dst_ip == other.dst_ip && dst_port == other.dst_port;
  }
};

struct FlowHash {
  std::size_t operator()(const Flow &f) const {
    std::size_t seed = 0;
    hash_combine(seed, f.src_ip);
    hash_combine(seed, f.src_port);
    hash_combine(seed, f.dst_ip);
    hash_combine(seed, f.dst_port);
    return seed;
  }
};

// A module of HTTP URL filtering. Ends an HTTP connection if the Host field
// matches the blacklist.
// igate/ogate 0: traffic from internal network to external network
// igate/ogate 1: traffic from external network to internal network
class UrlFilter final : public Module {
 public:
  typedef std::pair<std::string, std::string> Url;

  static const Commands cmds;
  static const gate_idx_t kNumIGates = 2;
  static const gate_idx_t kNumOGates = 2;

  pb_error_t Init(const bess::pb::UrlFilterArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  pb_cmd_response_t CommandAdd(const bess::pb::UrlFilterArg &arg);
  pb_cmd_response_t CommandClear(const bess::pb::EmptyArg &arg);

 private:
  std::vector<Url> blacklist_;
  std::unordered_map<Flow, TcpFlowReconstruct, FlowHash> flow_cache_;
  std::unordered_map<Flow, uint64_t, FlowHash> blocked_flows_;
};

#endif  // BESS_MODULES_URL_FILTER_H_
