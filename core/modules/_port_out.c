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

  static const std::vector<struct Command> cmds;

 private:
  struct port *port_ = {0};
  pkt_io_func_t send_pkts_ = {0};
};

const std::vector<struct Command> PortOut::cmds = {};

struct snobj *PortOut::Init(struct snobj *arg) {
  const char *port_name;

  int ret;

  if (!arg || !(port_name = snobj_eval_str(arg, "port")))
    return snobj_err(EINVAL, "'port' must be given as a string");

  this->port_ = find_port(port_name);
  if (!this->port_) return snobj_err(ENODEV, "Port %s not found", port_name);

  if (this->port_->num_queues[PACKET_DIR_OUT] == 0)
    return snobj_err(ENODEV, "Port %s has no outgoing queue", port_name);

  ret = acquire_queues(this->port_, reinterpret_cast<const module *>(this),
                       PACKET_DIR_OUT, NULL, 0);
  if (ret < 0) return snobj_errno(-ret);

  this->send_pkts_ = this->port_->driver->send_pkts;

  return NULL;
}

void PortOut::Deinit() {
  release_queues(this->port_, reinterpret_cast<const module *>(this),
                 PACKET_DIR_OUT, NULL, 0);
}

struct snobj *PortOut::GetDesc() {
  return snobj_str_fmt("%s/%s", this->port_->name, this->port_->driver->name);
}

void PortOut::ProcessBatch(struct pkt_batch *batch) {
  struct port *p = this->port_;

  /* TODO: choose appropriate out queue */
  const queue_t qid = 0;

  uint64_t sent_bytes = 0;
  int sent_pkts;

  sent_pkts = this->send_pkts_(p, qid, batch->pkts, batch->cnt);

  if (!(p->driver->flags & DRIVER_FLAG_SELF_OUT_STATS)) {
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

ModuleClassRegister<PortOut> port_out("PortOut", "port_out",
                                      "sends pakets to a port");
