#include "source.h"

const Commands Source::cmds = {
    {"set_pkt_size", "SourceCommandSetPktSizeArg",
     MODULE_CMD_FUNC(&Source::CommandSetPktSize), 1},
    {"set_burst", "SourceCommandSetBurstArg",
     MODULE_CMD_FUNC(&Source::CommandSetBurst), 1},
};

pb_error_t Source::Init(const bess::pb::SourceArg &arg) {
  pb_error_t err;

  task_id_t tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return pb_error(ENOMEM, "Task creation failed");

  pkt_size_ = 60;
  burst_ = bess::PacketBatch::kMaxBurst;

  if (arg.pkt_size() > 0) {
    if (arg.pkt_size() > SNBUF_DATA) {
      return pb_error(EINVAL, "Invalid packet size");
    }
    pkt_size_ = arg.pkt_size();
  }

  if (arg.burst() > 0) {
    if (arg.burst() > bess::PacketBatch::kMaxBurst) {
      return pb_error(EINVAL, "burst size must be [0,%zu]",
                      bess::PacketBatch::kMaxBurst);
    }
    burst_ = arg.burst();
  }

  return pb_errno(0);
}

pb_cmd_response_t Source::CommandSetBurst(
    const bess::pb::SourceCommandSetBurstArg &arg) {
  pb_cmd_response_t response;

  if (arg.burst() > bess::PacketBatch::kMaxBurst) {
    set_cmd_response_error(&response,
                           pb_error(EINVAL, "burst size must be [0,%zu]",
                                    bess::PacketBatch::kMaxBurst));
    return response;
  }
  burst_ = arg.burst();

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t Source::CommandSetPktSize(
    const bess::pb::SourceCommandSetPktSizeArg &arg) {
  pb_cmd_response_t response;

  uint64_t val = arg.pkt_size();
  if (val == 0 || val > SNBUF_DATA) {
    set_cmd_response_error(&response, pb_error(EINVAL, "Invalid packet size"));
    return response;
  }
  pkt_size_ = val;
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

struct task_result Source::RunTask(void *) {
  bess::PacketBatch batch;

  const int pkt_overhead = 24;

  const int pkt_size = ACCESS_ONCE(pkt_size_);
  const int burst = ACCESS_ONCE(burst_);

  int cnt = bess::Packet::Alloc(batch.pkts(), burst, pkt_size);

  batch.set_cnt(cnt);
  RunNextModule(&batch);  // it's fine to call this function with cnt==0

  return (struct task_result){
      .packets = static_cast<uint64_t>(cnt),
      .bits = static_cast<uint64_t>(pkt_size + pkt_overhead) * cnt * 8,
  };
}

ADD_MODULE(Source, "source",
           "infinitely generates packets with uninitialized data")
