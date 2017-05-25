#include "measure.h"

#include <cmath>

#include "../utils/common.h"
#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/time.h"
#include "../utils/udp.h"
#include "timestamp.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using bess::utils::Udp;

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
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&Measure::CommandClear), 0},
};

CommandResponse Measure::Init(const bess::pb::MeasureArg &arg) {
  // seconds from nanoseconds
  warmup_ns_ = arg.warmup() * 1000000000ull;

  if (arg.offset()) {
    offset_ = arg.offset();
  } else {
    offset_ = sizeof(Ethernet) + sizeof(Ipv4) + sizeof(Udp);
  }

  if (arg.jitter_sample_prob()) {
    jitter_sample_prob_ = arg.jitter_sample_prob();
  } else {
    jitter_sample_prob_ = kDefaultIpDvSampleProb;
  }

  start_ns_ = tsc_to_ns(rdtsc());

  return CommandSuccess();
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

        rtt_hist_.insert(diff);
        if (rand_.GetRealNonzero() <= jitter_sample_prob_) {
          if (unlikely(!last_rtt_ns_)) {
            last_rtt_ns_ = diff;
            continue;
          }
          uint64_t jitter = absdiff(diff, last_rtt_ns_);
          jitter_hist_.insert(jitter);
          last_rtt_ns_ = diff;
        }
      }
    }
  }
  RunNextModule(batch);
}

CommandResponse Measure::CommandGetSummary(const bess::pb::EmptyArg &) {
  uint64_t pkt_total = pkt_cnt_;
  uint64_t byte_total = bytes_cnt_;
  uint64_t bits = (byte_total + pkt_total * 24) * 8;

  bess::pb::MeasureCommandGetSummaryResponse r;

  r.set_timestamp(get_epoch_time());
  r.set_packets(pkt_total);
  r.set_bits(bits);
  r.set_total_latency_ns(total_latency_);
  r.set_latency_min_ns(rtt_hist_.min());
  r.set_latency_avg_ns(rtt_hist_.avg());
  r.set_latency_max_ns(rtt_hist_.max());
  r.set_latency_50_ns(rtt_hist_.percentile(50));
  r.set_latency_99_ns(rtt_hist_.percentile(99));
  r.set_jitter_min_ns(jitter_hist_.min());
  r.set_jitter_avg_ns(jitter_hist_.avg());
  r.set_jitter_max_ns(jitter_hist_.max());
  r.set_jitter_50_ns(jitter_hist_.percentile(50));
  r.set_jitter_99_ns(jitter_hist_.percentile(99));

  return CommandSuccess(r);
}

CommandResponse Measure::CommandClear(const bess::pb::EmptyArg &) {
  rtt_hist_.reset();
  jitter_hist_.reset();
  return CommandResponse();
}

ADD_MODULE(Measure, "measure",
           "measures packet latency (paired with Timestamp module)")
