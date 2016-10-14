#include <string.h>

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "../module.h"
#include "../time.h"
#include "../utils/histogram.h"

inline int get_measure_packet(struct snbuf *pkt, uint64_t *time) {
  uint8_t *avail = (static_cast<uint8_t *>(snb_head_data(pkt)) +
                    sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr)) +
                   sizeof(struct tcp_hdr);
  uint64_t *ts = reinterpret_cast<uint64_t *>(avail + 1);
  uint8_t available = *avail;
  *time = *ts;
  return available;
}

/* XXX: currently doesn't support multiple workers */
class Measure : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const std::vector<struct Command> cmds;

 private:
  struct snobj *CommandGetSummary(struct snobj *arg);

  struct histogram hist = {0};

  uint64_t start_time = {0};
  int warmup = {0}; /* second */

  uint64_t pkt_cnt = {0};
  uint64_t bytes_cnt = {0};
  uint64_t total_latency = {0};
};

const std::vector<struct Command> Measure::cmds = {
    {"get_summary", static_cast<CmdFunc>(&Measure::CommandGetSummary), 0},
};

struct snobj *Measure::Init(struct snobj *arg) {
  if (arg) this->warmup = snobj_eval_int(arg, "warmup");

  init_hist(&this->hist);

  return NULL;
}

void Measure::ProcessBatch(struct pkt_batch *batch) {
  uint64_t time = get_time();

  if (this->start_time == 0) this->start_time = get_time();

  if (static_cast<int>(HISTO_TIME_TO_SEC(time - this->start_time)) <
      this->warmup)
    goto skip;

  this->pkt_cnt += batch->cnt;

  for (int i = 0; i < batch->cnt; i++) {
    uint64_t pkt_time;
    if (get_measure_packet(batch->pkts[i], &pkt_time)) {
      uint64_t diff;

      if (time >= pkt_time)
        diff = time - pkt_time;
      else
        continue;

      this->bytes_cnt += batch->pkts[i]->mbuf.pkt_len;
      this->total_latency += diff;

      record_latency(&this->hist, diff);
    }
  }

skip:
  run_next_module(this, batch);
}

struct snobj *Measure::CommandGetSummary(struct snobj *arg) {
  uint64_t pkt_total = this->pkt_cnt;
  uint64_t byte_total = this->bytes_cnt;
  uint64_t bits = (byte_total + pkt_total * 24) * 8;

  struct snobj *r = snobj_map();

  snobj_map_set(r, "timestamp", snobj_double(get_epoch_time()));
  snobj_map_set(r, "packets", snobj_uint(pkt_total));
  snobj_map_set(r, "bits", snobj_uint(bits));
  snobj_map_set(r, "total_latency_ns", snobj_uint(this->total_latency * 100));

  return r;
}

ModuleClassRegister<Measure> measure(
    "Measure", "measure",
    "measures packet latency (paired with Timestamp module)");
