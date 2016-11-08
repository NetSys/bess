#ifndef BESS_MODULES_MEASURE_H_
#define BESS_MODULES_MEASURE_H_

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/histogram.h"

class Measure : public Module {
 public:
  Measure()
      : Module(),
        hist_(),
        start_time_(),
        warmup_(),
        pkt_cnt_(),
        bytes_cnt_(),
        total_latency_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::MeasureArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandGetSummary(struct snobj *arg);
  bess::pb::ModuleCommandResponse CommandGetSummaryPb(
      const bess::pb::EmptyArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

 private:
  struct histogram hist_ = {};

  uint64_t start_time_;
  int warmup_; /* second */

  uint64_t pkt_cnt_;
  uint64_t bytes_cnt_;
  uint64_t total_latency_;
};

#endif  // BESS_MODULES_MEASURE_H_
