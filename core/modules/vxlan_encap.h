#ifndef __VXLAN_ENCAP_H__
#define __VXLAN_ENCAP_H__

#include "../module.h"

class VXLANEncap : public Module {
 public:
  VXLANEncap() : Module(), dstport_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const bess::protobuf::VXLANEncapArg &arg);
  virtual void ProcessBatch(struct pkt_batch *batch);

  int num_attrs = 6;
  struct bess::metadata::mt_attr attrs[bess::metadata::kMaxAttrsPerModule] = {
      {
          .name = "tun_ip_src", .size = 4, .mode = bess::metadata::MT_READ,
      },
      {
          .name = "tun_ip_dst", .size = 4, .mode = bess::metadata::MT_READ,
      },
      {
          .name = "tun_id", .size = 4, .mode = bess::metadata::MT_READ,
      },
      {
          .name = "ip_src", .size = 4, .mode = bess::metadata::MT_WRITE,
      },
      {
          .name = "ip_dst", .size = 4, .mode = bess::metadata::MT_WRITE,
      },
      {
          .name = "ip_proto", .size = 1, .mode = bess::metadata::MT_WRITE,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;

 private:
  uint16_t dstport_;
};

#endif
