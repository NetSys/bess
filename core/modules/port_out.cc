#include "../module.h"
#include "../port.h"

class PortOut : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual struct snobj *GetDesc();

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 0;

  static const Commands<Module> cmds;

 private:
  Port *port_ = {};
};

const Commands<Module> PortOut::cmds = {};

struct snobj *PortOut::Init(struct snobj *arg) {
  const char *port_name;

  int ret;

  if (!arg || !(port_name = snobj_eval_str(arg, "port")))
    return snobj_err(EINVAL, "'port' must be given as a string");

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return snobj_err(ENODEV, "Port %s not found", port_name);
  }
  port_ = it->second;

  if (port_->num_queues[PACKET_DIR_OUT] == 0)
    return snobj_err(ENODEV, "Port %s has no outgoing queue", port_name);

  ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                             PACKET_DIR_OUT, NULL, 0);
  if (ret < 0) return snobj_errno(-ret);

  return NULL;
}

void PortOut::Deinit() {
  port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_OUT,
                       NULL, 0);
}

struct snobj *PortOut::GetDesc() {
  return snobj_str_fmt("%s/%s", port_->name().c_str(),
                       port_->port_builder()->class_name().c_str());
}

void PortOut::ProcessBatch(struct pkt_batch *batch) {
  Port *p = port_;

  /* TODO: choose appropriate out queue */
  const queue_t qid = 0;

  uint64_t sent_bytes = 0;
  int sent_pkts;

  sent_pkts = p->SendPackets(qid, batch->pkts, batch->cnt);

  if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
    const packet_dir_t dir = PACKET_DIR_OUT;

    for (int i = 0; i < sent_pkts; i++)
      sent_bytes += snb_total_len(batch->pkts[i]);

    p->queue_stats[dir][qid].packets += sent_pkts;
    p->queue_stats[dir][qid].dropped += (batch->cnt - sent_pkts);
    p->queue_stats[dir][qid].bytes += sent_bytes;
  }

  if (sent_pkts < batch->cnt)
    snb_free_bulk(batch->pkts + sent_pkts, batch->cnt - sent_pkts);
}

ADD_MODULE(PortOut, "port_out", "sends pakets to a port")
