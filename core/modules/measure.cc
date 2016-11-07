#include "measure.h"

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "../utils/time.h"

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

const Commands<Module> Measure::cmds = {
    {"get_summary", MODULE_FUNC &Measure::CommandGetSummary, 0},
};

const PbCommands Measure::pb_cmds = {
    {"get_summary", MODULE_CMD_FUNC(&Measure::CommandGetSummaryPb), 0},
};

struct snobj *Measure::Init(struct snobj *arg) {
  if (arg) {
    warmup_ = snobj_eval_int(arg, "warmup");
  }
  return nullptr;
}

pb_error_t Measure::InitPb(const bess::pb::MeasureArg &arg) {
  if (arg.warmup()) {
    warmup_ = arg.warmup();
  }
  return pb_errno(0);
}

void Measure::ProcessBatch(struct pkt_batch *batch) {
  uint64_t time = get_time();

  if (start_time_ == 0) {
    start_time_ = get_time();
  }

  if (static_cast<int>(HISTO_TIME_TO_SEC(time - start_time_)) < warmup_) {
    RunNextModule(batch);
    return;
  }

  pkt_cnt_ += batch->cnt;

  for (int i = 0; i < batch->cnt; i++) {
    uint64_t pkt_time;
    if (get_measure_packet(batch->pkts[i], &pkt_time)) {
      uint64_t diff;

      if (time >= pkt_time) {
        diff = time - pkt_time;
      } else {
        continue;
      }

      bytes_cnt_ += batch->pkts[i]->mbuf.pkt_len;
      total_latency_ += diff;

      record_latency(&hist_, diff);
    }
  }
}

struct snobj *Measure::CommandGetSummary(struct snobj *) {
  uint64_t pkt_total = pkt_cnt_;
  uint64_t byte_total = bytes_cnt_;
  uint64_t bits = (byte_total + pkt_total * 24) * 8;

  struct snobj *r = snobj_map();

  snobj_map_set(r, "timestamp", snobj_double(get_epoch_time()));
  snobj_map_set(r, "packets", snobj_uint(pkt_total));
  snobj_map_set(r, "bits", snobj_uint(bits));
  snobj_map_set(r, "total_latency_ns", snobj_uint(total_latency_ * 100));

  return r;
}

bess::pb::ModuleCommandResponse Measure::CommandGetSummaryPb(
    const bess::pb::EmptyArg &) {
  uint64_t pkt_total = pkt_cnt_;
  uint64_t byte_total = bytes_cnt_;
  uint64_t bits = (byte_total + pkt_total * 24) * 8;

  bess::pb::ModuleCommandResponse response;

  bess::pb::MeasureCommandGetSummaryResponse r;
  r.set_timestamp(get_epoch_time());
  r.set_packets(pkt_total);
  r.set_bits(bits);
  r.set_total_latency_ns(total_latency_ * 100);

  response.mutable_error()->set_err(0);
  response.mutable_other()->PackFrom(r);

  return response;
}

ADD_MODULE(Measure, "measure",
           "measures packet latency (paired with Timestamp module)")
