#include "queue_inc.h"

#include "../port.h"
#include "../utils/format.h"

const Commands QueueInc::cmds = {{"set_burst", "QueueIncCommandSetBurstArg",
                                  MODULE_CMD_FUNC(&QueueInc::CommandSetBurst),
                                  1}};

pb_error_t QueueInc::Init(const bess::pb::QueueIncArg &arg) {
  const char *port_name;
  task_id_t tid;
  pb_error_t err;
  burst_ = bess::PacketBatch::kMaxBurst;
  if (!arg.port().length()) {
    return pb_error(EINVAL, "Field 'port' must be specified");
  }
  port_name = arg.port().c_str();
  qid_ = arg.qid();

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return pb_error(ENODEV, "Port %s not found", port_name);
  }
  port_ = it->second;

  if (arg.burst() != 0) {
    err = SetBurst(arg.burst());
    if (err.err() != 0) {
      return err;
    }
  }

  if (arg.prefetch()) {
    prefetch_ = 1;
  }

  tid = RegisterTask((void *)(uintptr_t)qid_);
  if (tid == INVALID_TASK_ID)
    return pb_error(ENOMEM, "Task creation failed");

  int ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                                 PACKET_DIR_INC, &qid_, 1);
  if (ret < 0) {
    return pb_errno(-ret);
  }

  return pb_errno(0);
}

void QueueInc::DeInit() {
  if (port_) {
    port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_INC,
                         &qid_, 1);
  }
}

std::string QueueInc::GetDesc() const {
  return bess::utils::Format("%s:%hhu/%s", port_->name().c_str(), qid_,
                             port_->port_builder()->class_name().c_str());
}

struct task_result QueueInc::RunTask(void *arg) {
  Port *p = port_;

  const queue_t qid = (queue_t)(uintptr_t)arg;

  bess::PacketBatch batch;
  struct task_result ret;

  uint64_t received_bytes = 0;

  const int burst = ACCESS_ONCE(burst_);
  const int pkt_overhead = 24;

  uint64_t cnt;
  batch.set_cnt(p->RecvPackets(qid, batch.pkts(), burst));
  cnt = batch.cnt();

  if (cnt == 0) {
    ret.packets = 0;
    ret.bits = 0;
    return ret;
  }

  /* NOTE: we cannot skip this step since it might be used by scheduler */
  if (prefetch_) {
    for (uint64_t i = 0; i < cnt; i++) {
      received_bytes += batch.pkts()[i]->total_len();
      rte_prefetch0(batch.pkts()[i]->head_data());
    }
  } else {
    for (uint64_t i = 0; i < cnt; i++) {
      received_bytes += batch.pkts()[i]->total_len();
    }
  }

  ret = (struct task_result){
      .packets = cnt, .bits = (received_bytes + cnt * pkt_overhead) * 8,
  };

  if (!(p->GetFlags() & DRIVER_FLAG_SELF_INC_STATS)) {
    p->queue_stats[PACKET_DIR_INC][qid].packets += cnt;
    p->queue_stats[PACKET_DIR_INC][qid].bytes += received_bytes;
  }

  RunNextModule(&batch);

  return ret;
}

pb_error_t QueueInc::SetBurst(int64_t burst) {
  if (burst == 0 ||
      burst > static_cast<int64_t>(bess::PacketBatch::kMaxBurst)) {
    return pb_error(EINVAL, "burst size must be [1,%zu]",
                    bess::PacketBatch::kMaxBurst);
  }
  burst_ = burst;
  return pb_errno(0);
}

pb_cmd_response_t QueueInc::CommandSetBurst(
    const bess::pb::QueueIncCommandSetBurstArg &arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, SetBurst(arg.burst()));
  return response;
}

ADD_MODULE(QueueInc, "queue_inc",
           "receives packets from a port via a specific queue")
