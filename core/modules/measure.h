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

#ifndef BESS_MODULES_MEASURE_H_
#define BESS_MODULES_MEASURE_H_

#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../utils/histogram.h"
#include "../utils/mcslock.h"
#include "../utils/random.h"

class Measure final : public Module {
 public:
  Measure()
      : Module(),
        rtt_hist_(kBuckets, kBucketWidth),
        jitter_hist_(kBuckets, kBucketWidth),
        rand_(),
        jitter_sample_prob_(),
        last_rtt_ns_(),
        offset_(),
        pkt_cnt_(),
        bytes_cnt_() {
    max_allowed_workers_ = Worker::kMaxWorkers;
  }

  CommandResponse Init(const bess::pb::MeasureArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandGetSummary(
      const bess::pb::MeasureCommandGetSummaryArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

  static const Commands cmds;

 private:
  static const uint64_t kBucketWidth = 100;  // Measure in 100 ns units
  static const uint64_t kBuckets = 1000000;
  static constexpr double kDefaultIpDvSampleProb = 0.05;

  void Clear();

  Histogram<uint64_t> rtt_hist_;
  Histogram<uint64_t> jitter_hist_;

  Random rand_;
  double jitter_sample_prob_;
  uint64_t last_rtt_ns_;

  size_t offset_;  // in bytes

  uint64_t pkt_cnt_;
  uint64_t bytes_cnt_;

  mcslock lock_;
};

#endif  // BESS_MODULES_MEASURE_H_
