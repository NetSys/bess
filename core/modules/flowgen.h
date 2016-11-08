#ifndef BESS_MODULES_FLOWGEN_H_
#define BESS_MODULES_FLOWGEN_H_

#include <queue>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/random.h"

typedef std::pair<uint64_t, struct flow *> Event;
typedef std::priority_queue<Event, std::vector<Event>,
                            std::function<bool(Event, Event)>>
    EventQueue;

class FlowGen : public Module {
 public:
  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 1;

  enum Arrival {
    ARRIVAL_UNIFORM = 0,
    ARRIVAL_EXPONENTIAL,
  };

  enum Duration {
    DURATION_UNIFORM = 0,
    DURATION_PARETO,
  };

  FlowGen()
      : Module(),
        active_flows_(),
        allocated_flows_(),
        generated_flows_(),
        flows_(),
        flows_free_(),
        events_(),
        templ_(),
        template_size_(),
        rng_(),
        arrival_(),
        duration_(),
        quick_rampup_(),
        total_pps_(),
        flow_rate_(),
        flow_duration_(),
        concurrent_flows_(),
        flow_pps_(),
        flow_pkts_(),
        flow_gap_ns_(),
        pareto_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::FlowGenArg &arg);

  virtual void Deinit();

  virtual struct task_result RunTask(void *arg);

  std::string GetDesc() const;
  struct snobj *GetDump() const;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

 private:
  inline double NewFlowPkts();
  inline double MaxFlowPkts() const;
  inline uint64_t NextFlowArrival();
  inline struct flow *ScheduleFlow(uint64_t time_ns);
  void MeasureParetoMean();
  void PopulateInitialFlows();
  struct snbuf *FillPacket(struct flow *f);
  void GeneratePackets(struct pkt_batch *batch);
  struct snobj *InitFlowPoolOld();
  struct snobj *ProcessArguments(struct snobj *arg);

  pb_error_t InitFlowPool();
  pb_error_t ProcessArguments(const bess::pb::FlowGenArg &arg);

  int active_flows_;
  int allocated_flows_;
  uint64_t generated_flows_;
  struct flow *flows_;
  struct cdlist_head flows_free_;

  EventQueue events_;

  char *templ_;
  int template_size_;

  Random rng_;

  Arrival arrival_;
  Duration duration_;

  /* behavior parameters */
  int quick_rampup_;

  /* load parameters */
  double total_pps_;
  double flow_rate_;     /* in flows/s */
  double flow_duration_; /* in seconds */

  /* derived variables */
  double concurrent_flows_; /* expected # of flows */
  double flow_pps_;         /* packets/s/flow */
  double flow_pkts_;        /* flow_pps * flow_duration */
  double flow_gap_ns_;      /* == 10^9 / flow_rate */

  struct {
    double alpha;
    double inversed_alpha; /* 1.0 / alpha */
    double mean;           /* determined by alpha */
  } pareto_;
};

#endif  // BESS_MODULES_FLOWGEN_H_
