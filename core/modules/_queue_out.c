#include "../module.h"
#include "../port.h"

class QueueOut : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual struct snobj *GetDesc();

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 0;

  static const std::vector<struct Command> cmds;

 private:
  struct port *port;
  pkt_io_func_t send_pkts;
  queue_t qid;
};

const std::vector<struct Command> QueueOut::cmds = {};

struct snobj *QueueOut::Init(struct snobj *arg) {
  struct snobj *t;

  const char *port_name;

  int ret;

  if (!arg || snobj_type(arg) != TYPE_MAP)
    return snobj_err(EINVAL, "Argument must be a map");

  t = snobj_eval(arg, "port");
  if (!t || !(port_name = snobj_str_get(t)))
    return snobj_err(EINVAL, "Field 'port' must be specified");

  t = snobj_eval(arg, "qid");
  if (!t || snobj_type(t) != TYPE_INT)
    return snobj_err(EINVAL, "Field 'qid' must be specified");
  this->qid = snobj_uint_get(t);

  this->port = find_port(port_name);
  if (!this->port) return snobj_err(ENODEV, "Port %s not found", port_name);

  ret = acquire_queues(this->port, reinterpret_cast<const module *>(this),
                       PACKET_DIR_OUT, &this->qid, 1);
  if (ret < 0) return snobj_errno(-ret);

  this->send_pkts = this->port->driver->send_pkts;

  return NULL;
}

void QueueOut::Deinit() {
  release_queues(this->port, reinterpret_cast<const module *>(this),
                 PACKET_DIR_OUT, &this->qid, 1);
}

struct snobj *QueueOut::GetDesc() {
  return snobj_str_fmt("%s/%s", this->port->name, this->port->driver->name);
}

void QueueOut::ProcessBatch(struct pkt_batch *batch) {
  struct port *p = this->port;

  const queue_t qid = this->qid;

  uint64_t sent_bytes = 0;
  int sent_pkts;

  sent_pkts = this->send_pkts(p, qid, batch->pkts, batch->cnt);

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

ModuleClassRegister<QueueOut> queue_out(
    "QueueOut", "queue_out", "sends packets to a port via a specific queue");
