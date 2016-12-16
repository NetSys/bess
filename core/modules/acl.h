#ifndef BESS_MODULES_ACL_H_
#define BESS_MODULES_ACL_H_

#include <utility>
#include <vector>

#include "../module.h"
#include "../module_msg.pb.h"

typedef uint32_t IPAddress;
typedef std::pair<IPAddress, IPAddress> CIDRNetwork;

class ACL final : public Module {
 public:
  struct ACLRule {
    CIDRNetwork src_ip;
    CIDRNetwork dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    bool established;
    bool drop;
  };

  static const Commands cmds;

  pb_error_t Init(const bess::pb::ACLArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  pb_cmd_response_t CommandAdd(const bess::pb::ACLArg &arg);
  pb_cmd_response_t CommandClear(const bess::pb::EmptyArg &arg);

 private:
  std::vector<ACLRule> rules_;
};

#endif  // BESS_MODULES_ACL_H_
