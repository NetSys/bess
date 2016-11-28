#include "source.h"

const PbCommands Source::pb_cmds = {
    {"set_pkt_size", "SourceCommandSetPktSizeArg",
     MODULE_CMD_FUNC(&Source::CommandSetPktSizePb), 1},
    {"set_burst", "SourceCommandSetBurstArg",
     MODULE_CMD_FUNC(&Source::CommandSetBurstPb), 1},
};

pb_error_t Source::InitPb(const bess::pb::SourceArg &arg) {
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
      return pb_error(EINVAL, "burst size must be [1,%ld]",
                      bess::PacketBatch::kMaxBurst);
    }
    burst_ = arg.burst();
  }

  return pb_errno(0);
}

pb_cmd_response_t Source::CommandSetBurstPb(
    const bess::pb::SourceCommandSetBurstArg &arg) {
  pb_cmd_response_t response;

  uint64_t val = arg.burst();
  if (val == 0 || val > bess::PacketBatch::kMaxBurst) {
    set_cmd_response_error(&response,
                           pb_error(EINVAL, "burst size must be [1,%lu]",
                                    bess::PacketBatch::kMaxBurst));
    return response;
  }
  burst_ = val;
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_cmd_response_t Source::CommandSetPktSizePb(
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
  struct task_result ret;

  const int pkt_overhead = 24;

  const int pkt_size = ACCESS_ONCE(pkt_size_);
  const int burst = ACCESS_ONCE(burst_);

  uint64_t total_bytes = pkt_size * burst;

  int cnt = bess::Packet::Alloc(batch.pkts(), burst, pkt_size);

  if (cnt > 0) {
    batch.set_cnt(cnt);
    RunNextModule(&batch);
  }

  ret = (struct task_result){
      .packets = static_cast<uint64_t>(cnt),
      .bits = (total_bytes + cnt * pkt_overhead) * 8,
  };

  return ret;
}

ADD_MODULE(Source, "source",
           "infinitely generates packets with uninitialized data")
