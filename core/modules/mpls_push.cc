#include "mpls_push.h"

#include "../utils/endian.h"
#include "../utils/ether.h"
#include "../utils/mpls.h"

using bess::utils::Ethernet;
using bess::utils::be16_t;
using bess::utils::Mpls;

const Commands MPLSPush::cmds = {{"set", "MplsPushArg",
                                  MODULE_CMD_FUNC(&MPLSPush::CommandSet),
                                  Command::THREAD_UNSAFE}};

MPLSPush::MPLSPush() : label_(0), ttl_(64), tc_(0), is_bottom_of_stack_(true) {}


CommandResponse MPLSPush::Init(const bess::pb::MplsPushArg &arg) {
  return CommandSet(arg);
}

void MPLSPush::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    Ethernet *eth = pkt->head_data<Ethernet *>();

    Ethernet::Address src_addr = eth->src_addr;
    Ethernet::Address dst_addr = eth->dst_addr;

    pkt->prepend(4);
    eth = pkt->head_data<Ethernet *>();
    eth->src_addr = src_addr;
    eth->dst_addr = dst_addr;
    eth->ether_type = be16_t(Ethernet::Type::kMpls);

    Mpls *mpls_hdr = reinterpret_cast<Mpls *>(eth + 1);
    mpls_hdr->SetEntry(label_, ttl_, tc_, is_bottom_of_stack_);
  }

  RunNextModule(batch);
}

CommandResponse MPLSPush::CommandSet(const bess::pb::MplsPushArg &arg) {
  label_ = arg.label();
  ttl_ = arg.ttl();
  is_bottom_of_stack_ = arg.is_bottom_of_stack();
  tc_ = arg.tc();
  return CommandSuccess();
}

ADD_MODULE(MPLSPush, "mpls_push", "Push MPLS label")
