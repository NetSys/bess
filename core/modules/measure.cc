// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "measure.h"

#include <iterator>

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

const Commands Measure::cmds = {
    {"get_summary", "MeasureCommandGetSummaryArg",
     MODULE_CMD_FUNC(&Measure::CommandGetSummary), Command::THREAD_SAFE},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&Measure::CommandClear),
     Command::THREAD_SAFE},
};

CommandResponse Measure::Init(const bess::pb::MeasureArg &arg) {
  // seconds from nanoseconds
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

  mcs_lock_init(&lock_);

  return CommandSuccess();
}

void Measure::ProcessBatch(bess::PacketBatch *batch) {
  // We don't use ctx->current_ns here for better accuracy
  uint64_t now_ns = tsc_to_ns(rdtsc());
  size_t offset = offset_;

  mcslock_node_t mynode;
  mcs_lock(&lock_, &mynode);

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

      rtt_hist_.Insert(diff);
      if (rand_.GetRealNonzero() <= jitter_sample_prob_) {
        if (unlikely(!last_rtt_ns_)) {
          last_rtt_ns_ = diff;
          continue;
        }
        uint64_t jitter = absdiff(diff, last_rtt_ns_);
        jitter_hist_.Insert(jitter);
        last_rtt_ns_ = diff;
      }
    }
  }

  mcs_unlock(&lock_, &mynode);

  RunNextModule(batch);
}

template <typename T>
static void SetHistogram(
    bess::pb::MeasureCommandGetSummaryResponse::Histogram *r, const T &hist) {
  r->set_count(hist.count);
  r->set_above_range(hist.above_range);
  r->set_min_ns(hist.min);
  r->set_max_ns(hist.max);
  r->set_avg_ns(hist.avg);
  r->set_total_ns(hist.total);
  for (const auto &val : hist.percentile_values) {
    r->add_percentile_values_ns(val);
  }
}

void Measure::Clear() {
  // vector initialization is expensive thus should be out of critical section
  decltype(rtt_hist_) new_rtt_hist(kBuckets, kBucketWidth);
  decltype(jitter_hist_) new_jitter_hist(kBuckets, kBucketWidth);

  // Use move semantics to minimize critical section
  mcslock_node_t mynode;
  mcs_lock(&lock_, &mynode);
  pkt_cnt_ = 0;
  bytes_cnt_ = 0;
  rtt_hist_ = std::move(new_rtt_hist);
  jitter_hist_ = std::move(new_jitter_hist);
  mcs_unlock(&lock_, &mynode);
}

static bool IsValidPercentiles(const std::vector<double> &percentiles) {
  if (percentiles.empty()) {
    return true;
  }

  return std::is_sorted(percentiles.cbegin(), percentiles.cend()) &&
         *std::min_element(percentiles.cbegin(), percentiles.cend()) >= 0.0 &&
         *std::max_element(percentiles.cbegin(), percentiles.cend()) <= 100.0;
}

CommandResponse Measure::CommandGetSummary(
    const bess::pb::MeasureCommandGetSummaryArg &arg) {
  bess::pb::MeasureCommandGetSummaryResponse r;

  std::vector<double> latency_percentiles;
  std::vector<double> jitter_percentiles;

  std::copy(arg.latency_percentiles().begin(), arg.latency_percentiles().end(),
            back_inserter(latency_percentiles));
  std::copy(arg.jitter_percentiles().begin(), arg.jitter_percentiles().end(),
            back_inserter(jitter_percentiles));

  if (!IsValidPercentiles(latency_percentiles)) {
    return CommandFailure(EINVAL, "invalid 'latency_percentiles'");
  }

  if (!IsValidPercentiles(jitter_percentiles)) {
    return CommandFailure(EINVAL, "invalid 'jitter_percentiles'");
  }

  r.set_timestamp(get_epoch_time());
  r.set_packets(pkt_cnt_);
  r.set_bits((bytes_cnt_ + pkt_cnt_ * 24) * 8);
  const auto &rtt = rtt_hist_.Summarize(latency_percentiles);
  const auto &jitter = jitter_hist_.Summarize(jitter_percentiles);

  SetHistogram(r.mutable_latency(), rtt);
  SetHistogram(r.mutable_jitter(), jitter);

  if (arg.clear()) {
    // Note that some samples might be lost due to the small gap between
    // Summarize() and the next mcs_lock... but we posit that smaller
    // critical section is more important.
    Clear();
  }

  return CommandSuccess(r);
}

CommandResponse Measure::CommandClear(const bess::pb::EmptyArg &) {
  Clear();
  return CommandResponse();
}

ADD_MODULE(Measure, "measure",
           "measures packet latency (paired with Timestamp module)")
