#ifndef BESS_MODULES_ARP_RESPONDER_H_
#define BESS_MODULES_ARP_RESPONDER_H_

#include <map>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/endian.h"
#include "../utils/ether.h"
#include "../utils/ip.h"

using bess::utils::Ethernet;
using bess::utils::be32_t;

// ARP cache entry struct which keeps mapping between IP and MAC
struct arp_entry {
  Ethernet::Address mac_addr;
  be32_t ip_addr;
  uint64_t time;  // timestamp used to expire cache entries (in milliseconds)
};

// ARP Responder module
// Answer ARP requests from an internal configurable cache
// Currently drops non ARP packets
class ArpResponder final : public Module {
 public:
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands cmds;

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const bess::pb::ArpResponderArg &arg);

 private:
  // Mapping between IP (key) and its ARP entry (MAC Address)
  std::map<be32_t, struct arp_entry> entries_;
};

#endif  // BESS_MODULES_ARP_RESPONDER_H_
