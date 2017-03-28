#ifndef BESS_MODULES_FLOWGEN_H_
#define BESS_MODULES_FLOWGEN_H_

#include <queue>
#include <stack>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/random.h"

typedef std::pair<uint64_t, struct flow *> Event;
typedef std::priority_queue<Event, std::vector<Event>,
                            std::greater<Event>>
    EventQueue;

struct flow {
  int packets_left;
  bool first_pkt;
  uint32_t next_seq_no;
  /* Note that these are in NETWORK ORDER */
  uint32_t src_ip, dst_ip;
  uint16_t src_port, dst_port;
};

class FlowGen final : public Module {
 public:
  enum class Arrival {
    kUniform = 0,
    kExponential,
  };

  enum class Duration {
    kUniform = 0,
    kPareto,
  };

  static const gate_idx_t kNumIGates = 0;

  FlowGen()
      : Module(),
        active_flows_(),
        generated_flows_(),
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

  static const Commands cmds;
  pb_error_t Init(const bess::pb::FlowGenArg &arg);
  pb_cmd_response_t CommandUpdate(const bess::pb::FlowGenArg &arg);

  void DeInit() override;

  struct task_result RunTask(void *arg) override;

  std::string GetDesc() const override;

 private:
  void UpdateDerivedParameters();
  double NewFlowPkts();
  double MaxFlowPkts() const;
  uint64_t NextFlowArrival();
  struct flow *ScheduleFlow(uint64_t time_ns);
  void MeasureParetoMean();
  void PopulateInitialFlows();

  pb_error_t UpdateBaseAddresses();
  bess::Packet *FillPacket(struct flow *f);
  void GeneratePackets(bess::PacketBatch *batch);

  pb_error_t ProcessArguments(const bess::pb::FlowGenArg &arg);

  // the number of concurrent flows
  int active_flows_;
  // the total number of flows generated so far (statistics only)
  uint64_t generated_flows_;
  // pool of free flow structs. LIFO for temporal locality.
  std::stack<struct flow *> flows_free_;

  // Priority queue of future events
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

  /* ranges over which to vary ips and ports */
  uint32_t ip_src_range_;
  uint32_t ip_dst_range_;
  uint16_t port_src_range_;
  uint16_t port_dst_range_;

  /* base ip and ports IN HOST ORDER */
  uint32_t ip_src_base_;
  uint32_t ip_dst_base_;
  uint16_t port_src_base_;
  uint16_t port_dst_base_;

  struct {
    double alpha;
    double inversed_alpha; /* 1.0 / alpha */
    double mean;           /* determined by alpha */
  } pareto_;
};

#endif  // BESS_MODULES_FLOWGEN_H_
