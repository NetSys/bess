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

#ifndef BESS_MODULES_FLOWGEN_H_
#define BESS_MODULES_FLOWGEN_H_

#include "../module.h"
#include "../pb/module_msg.pb.h"

#include <queue>
#include <stack>

#include "../utils/endian.h"
#include "../utils/random.h"

typedef std::pair<uint64_t, struct flow *> Event;
typedef std::priority_queue<Event, std::vector<Event>, std::greater<Event>>
    EventQueue;

struct flow {
  int packets_left;
  bool first_pkt;

  uint32_t next_seq_no;
  bess::utils::be32_t src_ip, dst_ip;
  bess::utils::be16_t src_port, dst_port;
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
        pareto_(),
        burst_() {
    is_task_ = true;
  }

  static const Commands cmds;
  CommandResponse Init(const bess::pb::FlowGenArg &arg);
  CommandResponse CommandUpdate(const bess::pb::FlowGenArg &arg);
  CommandResponse CommandSetBurst(
      const bess::pb::FlowGenCommandSetBurstArg &arg);

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

  CommandResponse UpdateBaseAddresses();
  bess::Packet *FillPacket(struct flow *f);
  void GeneratePackets(bess::PacketBatch *batch);

  CommandResponse ProcessArguments(const bess::pb::FlowGenArg &arg);

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

  int burst_;
};

#endif  // BESS_MODULES_FLOWGEN_H_
