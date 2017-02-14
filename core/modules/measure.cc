#include "measure.h"

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "../utils/time.h"

#define MEASURE_ONE_NS (1000lu * 1000 * 1000)
#define MEASURE_TIME_TO_SEC(t) ((t) / (MEASURE_ONE_NS))

inline bool get_measure_packet(bess::Packet *pkt, size_t offset,
                              uint64_t *time) {
  const uint16_t kMarker = 0x54C5;
  uint16_t *marker = reinterpret_cast<uint16_t*>(pkt->head_data<uint8_t *>() + offset);
  if (*marker == kMarker) {
    *time = *reinterpret_cast<uint64_t*>(marker + 1);
    return true;
  }
  return false;
}

/* XXX: currently doesn't support multiple workers */

const Commands Measure::cmds = {
    {"get_summary", "EmptyArg", MODULE_CMD_FUNC(&Measure::CommandGetSummary),
     0},
};

pb_error_t Measure::Init(const bess::pb::MeasureArg &arg) {
  if (arg.warmup()) {
    warmup_ = arg.warmup();
  }
  if (arg.offset()) {
    offset_ = arg.offset();
  } else {
    offset_ = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) +
              sizeof(struct tcp_hdr);
  }
  return pb_errno(0);
}

void Measure::ProcessBatch(bess::PacketBatch *batch) {
  uint64_t time = tsc_to_ns(rdtsc());
  size_t offset = offset_;

  if (start_time_ == 0) {
    start_time_ = time;
  }

  if (static_cast<int>(MEASURE_TIME_TO_SEC(time - start_time_)) >= warmup_) {
    pkt_cnt_ += batch->cnt();

    for (int i = 0; i < batch->cnt(); i++) {
      uint64_t pkt_time;
      if (get_measure_packet(batch->pkts()[i], offset, &pkt_time)) {
        uint64_t diff;

        if (time >= pkt_time) {
          diff = time - pkt_time;
        } else {
          continue;
        }

        bytes_cnt_ += batch->pkts()[i]->total_len();
        total_latency_ += diff;

        hist_.insert(diff);
      }
    }
  }
  RunNextModule(batch);
}

pb_cmd_response_t Measure::CommandGetSummary(const bess::pb::EmptyArg &) {
  uint64_t pkt_total = pkt_cnt_;
  uint64_t byte_total = bytes_cnt_;
  uint64_t bits = (byte_total + pkt_total * 24) * 8;

  pb_cmd_response_t response;

  bess::pb::MeasureCommandGetSummaryResponse r;

  r.set_timestamp(get_epoch_time());
  r.set_packets(pkt_total);
  r.set_bits(bits);
  r.set_total_latency_ns(total_latency_);
  r.set_latency_min_ns(hist_.min());
  r.set_latency_avg_ns(hist_.avg());
  r.set_latency_max_ns(hist_.max());
  r.set_latency_50_ns(hist_.percentile(50));
  r.set_latency_99_ns(hist_.percentile(99));

  response.mutable_error()->set_err(0);
  response.mutable_other()->PackFrom(r);

  return response;
}

ADD_MODULE(Measure, "measure",
           "measures packet latency (paired with Timestamp module)")
