#include "port_out.h"
#include "../utils/format.h"

CommandResponse PortOut::Init(const bess::pb::PortOutArg &arg) {
  const char *port_name;
  int ret;

  if (!arg.port().length()) {
    return CommandFailure(EINVAL, "'port' must be given as a string");
  }

  port_name = arg.port().c_str();

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return CommandFailure(ENODEV, "Port %s not found", port_name);
  }
  port_ = it->second;

  if (port_->num_queues[PACKET_DIR_OUT] == 0) {
    return CommandFailure(ENODEV, "Port %s has no outgoing queue", port_name);
  }

  ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                             PACKET_DIR_OUT, nullptr, 0);

  node_constraints_ = port_->GetNodePlacementConstraint();

  if (ret < 0) {
    return CommandFailure(-ret);
  }

  return CommandSuccess();
}

void PortOut::DeInit() {
  if (port_) {
    port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_OUT,
                         nullptr, 0);
  }
}

std::string PortOut::GetDesc() const {
  return bess::utils::Format("%s/%s", port_->name().c_str(),
                             port_->port_builder()->class_name().c_str());
}

void PortOut::ProcessBatch(bess::PacketBatch *batch) {
  Port *p = port_;

  /* TODO: choose appropriate out queue */
  const queue_t qid = 0;

  uint64_t sent_bytes = 0;
  int sent_pkts;

  sent_pkts = p->SendPackets(qid, batch->pkts(), batch->cnt());

  if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
    const packet_dir_t dir = PACKET_DIR_OUT;

    for (int i = 0; i < sent_pkts; i++)
      sent_bytes += batch->pkts()[i]->total_len();

    p->queue_stats[dir][qid].packets += sent_pkts;
    p->queue_stats[dir][qid].dropped += (batch->cnt() - sent_pkts);
    p->queue_stats[dir][qid].bytes += sent_bytes;
  }

  if (sent_pkts < batch->cnt()) {
    bess::Packet::Free(batch->pkts() + sent_pkts, batch->cnt() - sent_pkts);
  }
}

ADD_MODULE(PortOut, "port_out", "sends pakets to a port")
