#include "../module.h"
#include "../port.h"

class PortInc : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void Deinit();

  virtual struct task_result RunTask(void *arg);

  virtual struct snobj *GetDesc();

  struct snobj *CommandSetBurst(struct snobj *arg);

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;

 private:
  Port *port_ = {};
  int prefetch_ = {};
  int burst_ = {};
};

const Commands<Module> PortInc::cmds = {
    {"set_burst", MODULE_FUNC &PortInc::CommandSetBurst, 1},
};

struct snobj *PortInc::Init(struct snobj *arg) {
  const char *port_name;
  queue_t num_inc_q;

  int ret;

  struct snobj *t;
  struct snobj *err;

  burst_ = MAX_PKT_BURST;

  if (!arg || !(port_name = snobj_eval_str(arg, "port")))
    return snobj_err(EINVAL, "'port' must be given as a string");

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return snobj_err(ENODEV, "Port %s not found", port_name);
  }
  port_ = it->second;

  if ((t = snobj_eval(arg, "burst")) != NULL) {
    err = CommandSetBurst(t);
    if (err) return err;
  }

  num_inc_q = port_->num_queues[PACKET_DIR_INC];
  if (num_inc_q == 0)
    return snobj_err(ENODEV, "Port %s has no incoming queue", port_name);

  for (queue_t qid = 0; qid < num_inc_q; qid++) {
    task_id_t tid = RegisterTask((void *)(uintptr_t)qid);

    if (tid == INVALID_TASK_ID)
      return snobj_err(ENOMEM, "Task creation failed");
  }

  if (snobj_eval_int(arg, "prefetch")) prefetch_ = 1;

  ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                             PACKET_DIR_INC, NULL, 0);
  if (ret < 0) return snobj_errno(-ret);

  return NULL;
}

void PortInc::Deinit() {
  port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_INC,
                       NULL, 0);
}

struct snobj *PortInc::GetDesc() {
  return snobj_str_fmt("%s/%s", port_->name().c_str(),
                       port_->port_builder()->class_name().c_str());
}

struct task_result PortInc::RunTask(void *arg) {
  Port *p = port_;

  const queue_t qid = (queue_t)(uintptr_t)arg;

  struct pkt_batch batch;
  struct task_result ret;

  uint64_t received_bytes = 0;

  const int burst = ACCESS_ONCE(burst_);
  const int pkt_overhead = 24;

  uint64_t cnt;

  cnt = batch.cnt = p->RecvPackets(qid, batch.pkts, burst);

  if (cnt == 0) {
    ret.packets = 0;
    ret.bits = 0;
    return ret;
  }

  /* NOTE: we cannot skip this step since it might be used by scheduler */
  if (prefetch_) {
    for (uint64_t i = 0; i < cnt; i++) {
      received_bytes += snb_total_len(batch.pkts[i]);
      rte_prefetch0(snb_head_data(batch.pkts[i]));
    }
  } else {
    for (uint64_t i = 0; i < cnt; i++)
      received_bytes += snb_total_len(batch.pkts[i]);
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

struct snobj *PortInc::CommandSetBurst(struct snobj *arg) {
  uint64_t val;

  if (snobj_type(arg) != TYPE_INT)
    return snobj_err(EINVAL, "burst must be an integer");

  val = snobj_uint_get(arg);

  if (val == 0 || val > MAX_PKT_BURST)
    return snobj_err(EINVAL, "burst size must be [1,%d]", MAX_PKT_BURST);

  burst_ = val;

  return NULL;
}

ADD_MODULE(PortInc, "port_inc", "receives packets from a port")
