#ifndef BESS_MODULES_MEASURE_H_
#define BESS_MODULES_MEASURE_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/histogram.h"

class Measure final : public Module {
 public:
  Measure()
      : Module(),
        hist_(Histogram<uint64_t>(kBuckets, kBucketWidth)),
        start_ns_(),
        warmup_ns_(),
        offset_(),
        pkt_cnt_(),
        bytes_cnt_(),
        total_latency_() {}

  pb_error_t Init(const bess::pb::MeasureArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  pb_cmd_response_t CommandGetSummary(const bess::pb::EmptyArg &arg);

  static const Commands cmds;

 private:
  static const uint64_t kBucketWidth = 100;  // Measure in 100 ns units
  static const uint64_t kBuckets = 1000000;

  Histogram<uint64_t> hist_;

  uint64_t start_ns_;
  uint64_t warmup_ns_;  // no measurement for this warmup period
  size_t offset_;       // in bytes

  uint64_t pkt_cnt_;
  uint64_t bytes_cnt_;
  uint64_t total_latency_;
};

#endif  // BESS_MODULES_MEASURE_H_
