#include "queue_out.h"

#include "../port.h"
#include "../utils/format.h"

CommandResponse QueueOut::Init(const bess::pb::QueueOutArg &arg) {
  const char *port_name;
  int ret;

  if (!arg.port().length()) {
    return CommandFailure(EINVAL, "Field 'port' must be specified");
  }

  port_name = arg.port().c_str();
  qid_ = arg.qid();

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return CommandFailure(ENODEV, "Port %s not found", port_name);
  }
  port_ = it->second;

  node_constraints_ = port_->GetNodePlacementConstraint();

  ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                             PACKET_DIR_OUT, &qid_, 1);
  if (ret < 0) {
    return CommandFailure(-ret);
  }

  return CommandSuccess();
}

void QueueOut::DeInit() {
  if (port_) {
    port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_OUT,
                         &qid_, 1);
  }
}

std::string QueueOut::GetDesc() const {
  return bess::utils::Format("%s/%s", port_->name().c_str(),
                             port_->port_builder()->class_name().c_str());
}

void QueueOut::ProcessBatch(bess::PacketBatch *batch) {
  Port *p = port_;

  const queue_t qid = qid_;

  uint64_t sent_bytes = 0;
  int sent_pkts;

  sent_pkts = p->SendPackets(qid, batch->pkts(), batch->cnt());

  if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
    const packet_dir_t dir = PACKET_DIR_OUT;

    for (int i = 0; i < sent_pkts; i++) {
      sent_bytes += batch->pkts()[i]->total_len();
    }

    p->queue_stats[dir][qid].packets += sent_pkts;
    p->queue_stats[dir][qid].dropped += (batch->cnt() - sent_pkts);
    p->queue_stats[dir][qid].bytes += sent_bytes;
  }

  if (sent_pkts < batch->cnt()) {
    bess::Packet::Free(batch->pkts() + sent_pkts, batch->cnt() - sent_pkts);
  }
}

ADD_MODULE(QueueOut, "queue_out",
           "sends packets to a port via a specific queue")
