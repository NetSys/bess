#include "source.h"

const Commands<Module> Source::cmds = {
    {"set_pkt_size", MODULE_FUNC &Source::command_set_pkt_size, 1},
    {"set_burst", MODULE_FUNC &Source::command_set_burst, 1},
};

const PbCommands Source::pb_cmds = {
    {"set_pkt_size", MODULE_CMD_FUNC(&Source::CommandSetPktSizePb), 1},
    {"set_burst", MODULE_CMD_FUNC(&Source::CommandSetBurstPb), 1},
};

pb_error_t Source::InitPb(const bess::pb::SourceArg &arg) {
  pb_error_t err;
  pb_cmd_response_t response;

  task_id_t tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return pb_error(ENOMEM, "Task creation failed");

  pkt_size_ = 60;
  burst_ = MAX_PKT_BURST;

  response = CommandSetPktSizePb(arg.pkt_size_arg());
  err = response.error();
  if (err.err() != 0) {
    return err;
  }

  response = CommandSetBurstPb(arg.burst_arg());
  err = response.error();
  if (err.err() != 0) {
    return err;
  }

  return pb_errno(0);
}

pb_cmd_response_t Source::CommandSetBurstPb(
    const bess::pb::SourceCommandSetBurstArg &arg) {
  pb_cmd_response_t response;

  uint64_t val = arg.burst();
  if (val == 0 || val > MAX_PKT_BURST) {
    set_cmd_response_error(
        &response,
        pb_error(EINVAL, "burst size must be [1,%d]", MAX_PKT_BURST));
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

struct snobj *Source::Init(struct snobj *arg) {
  struct snobj *t;
  struct snobj *err;

  task_id_t tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return snobj_err(ENOMEM, "Task creation failed");

  pkt_size_ = 60;
  burst_ = MAX_PKT_BURST;

  if (!arg) {
    return nullptr;
  }

  if ((t = snobj_eval(arg, "pkt_size")) != nullptr) {
    err = command_set_pkt_size(t);
    if (err) {
      return err;
    }
  }

  if ((t = snobj_eval(arg, "burst")) != nullptr) {
    err = command_set_burst(t);
    if (err) {
      return err;
    }
  }

  return nullptr;
}

struct task_result Source::RunTask(void *) {
  struct pkt_batch batch;
  struct task_result ret;

  const int pkt_overhead = 24;

  const int pkt_size = ACCESS_ONCE(pkt_size_);
  const int burst = ACCESS_ONCE(burst_);

  uint64_t total_bytes = pkt_size * burst;

  int cnt = snb_alloc_bulk(batch.pkts, burst, pkt_size);

  if (cnt > 0) {
    batch.cnt = cnt;
    RunNextModule(&batch);
  }

  ret = (struct task_result){
      .packets = static_cast<uint64_t>(cnt),
      .bits = (total_bytes + cnt * pkt_overhead) * 8,
  };

  return ret;
}

struct snobj *Source::command_set_pkt_size(struct snobj *arg) {
  uint64_t val;

  if (snobj_type(arg) != TYPE_INT) {
    return snobj_err(EINVAL, "pkt_size must be an integer");
  }

  val = snobj_uint_get(arg);

  if (val == 0 || val > SNBUF_DATA) {
    return snobj_err(EINVAL, "Invalid packet size");
  }

  pkt_size_ = val;

  return nullptr;
}

struct snobj *Source::command_set_burst(struct snobj *arg) {
  uint64_t val;

  if (snobj_type(arg) != TYPE_INT) {
    return snobj_err(EINVAL, "burst must be an integer");
  }

  val = snobj_uint_get(arg);

  if (val == 0 || val > MAX_PKT_BURST) {
    return snobj_err(EINVAL, "burst size must be [1,%d]", MAX_PKT_BURST);
  }

  burst_ = val;

  return nullptr;
}

ADD_MODULE(Source, "source",
           "infinitely generates packets with uninitialized data")
