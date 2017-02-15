#include "measure.h"

#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/time.h"
#include "../utils/udp.h"
#include "timestamp.h"

using bess::utils::EthHeader;
using bess::utils::Ipv4Header;
using bess::utils::UdpHeader;

static bool IsTimestamped(bess::Packet *pkt, size_t offset, uint64_t *time) {
  auto *marker = pkt->head_data<Timestamp::MarkerType *>(offset);

  if (*marker == Timestamp::kMarker) {
    *time = *reinterpret_cast<uint64_t *>(marker + 1);
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
  // seconds from nanoseconds
  warmup_ns_ = arg.warmup() * 1000000000ul;

  if (arg.offset()) {
    offset_ = arg.offset();
  } else {
    offset_ = sizeof(struct EthHeader) + sizeof(struct Ipv4Header) +
              sizeof(struct UdpHeader);
  }

  start_ns_ = tsc_to_ns(rdtsc());

  return pb_errno(0);
}

void Measure::ProcessBatch(bess::PacketBatch *batch) {
  // We don't use ctx->current_ns here for better accuracy
  uint64_t now_ns = tsc_to_ns(rdtsc());
  size_t offset = offset_;

  if (now_ns - start_ns_ >= warmup_ns_) {
    pkt_cnt_ += batch->cnt();

    for (int i = 0; i < batch->cnt(); i++) {
      uint64_t pkt_time;
      if (IsTimestamped(batch->pkts()[i], offset, &pkt_time)) {
        uint64_t diff;

        if (now_ns >= pkt_time) {
          diff = now_ns - pkt_time;
        } else {
          // The magic number matched, but timestamp doesn't seem correct
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
