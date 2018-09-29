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

#include "flowgen.h"

#include <cmath>
#include <functional>

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/format.h"
#include "../utils/ip.h"
#include "../utils/simd.h"
#include "../utils/tcp.h"
#include "../utils/udp.h"
#include "../utils/time.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using bess::utils::Tcp;
using bess::utils::Udp;
using bess::utils::be16_t;
using bess::utils::be32_t;

/* we ignore the last 1% tail to make the variance finite */
const double PARETO_TAIL_LIMIT = 0.99;

const Commands FlowGen::cmds = {
    {"update", "FlowGenArg", MODULE_CMD_FUNC(&FlowGen::CommandUpdate),
     Command::THREAD_UNSAFE},
    {"set_burst", "FlowGenCommandSetBurstArg",
     MODULE_CMD_FUNC(&FlowGen::CommandSetBurst), Command::THREAD_SAFE}};

/* find x from CDF of pareto distribution from given y in [0.0, 1.0) */
static inline double pareto_variate(double inversed_alpha, double y) {
  return pow(1.0 / (1.0 - y * PARETO_TAIL_LIMIT), inversed_alpha);
}

static inline double scaled_pareto_variate(double inversed_alpha, double mean,
                                           double desired_mean, double y) {
  double x = pareto_variate(inversed_alpha, y);

  return 1.0 + (x - 1.0) / (mean - 1.0) * (desired_mean - 1.0);
}

inline double FlowGen::NewFlowPkts() {
  switch (duration_) {
    case Duration::kUniform:
      return flow_pkts_;
    case Duration::kPareto:
      return scaled_pareto_variate(pareto_.inversed_alpha, pareto_.mean,
                                   flow_pkts_, rng_.GetReal());
    default:
      CHECK(0);
  }
  return 0;
}

inline double FlowGen::MaxFlowPkts() const {
  switch (duration_) {
    case Duration::kUniform:
      return flow_pkts_;
    case Duration::kPareto:
      return scaled_pareto_variate(pareto_.inversed_alpha, pareto_.mean,
                                   flow_pkts_, 1.0);
    default:
      CHECK(0);
  }
  return 0;
}

inline uint64_t FlowGen::NextFlowArrival() {
  switch (arrival_) {
    case Arrival::kUniform:
      return flow_gap_ns_;
    case Arrival::kExponential:
      return -log(rng_.GetRealNonzero()) * flow_gap_ns_;
    default:
      CHECK(0);
  }
  return 0;
}

// should never fail, always returning a non-null pointer
inline flow *FlowGen::ScheduleFlow(uint64_t time_ns) {
  struct flow *f;

  if (flows_free_.empty()) {
    f = new flow();
  } else {
    f = flows_free_.top();
    flows_free_.pop();
  }

  f->first_pkt = true;
  f->next_seq_no = 12345;
  f->src_ip = be32_t(ip_src_base_ + rng_.GetRange(ip_src_range_));
  f->dst_ip = be32_t(ip_dst_base_ + rng_.GetRange(ip_dst_range_));
  f->src_port = be16_t(port_src_base_ + rng_.GetRange(port_src_range_));
  f->dst_port = be16_t(port_dst_base_ + rng_.GetRange(port_dst_range_));

  /* compensate the fraction part by adding [0.0, 1.0) */
  f->packets_left = NewFlowPkts() + rng_.GetReal();

  active_flows_++;
  generated_flows_++;

  events_.emplace(time_ns, f);

  return f;
}

void FlowGen::MeasureParetoMean() {
  const int iteration = 1000000;
  double total = 0.0;

  for (int i = 0; i <= iteration; i++) {
    double y = i / (double)iteration;
    double x = pareto_variate(pareto_.inversed_alpha, y);
    total += x;
  }

  pareto_.mean = total / (iteration + 1);
}

void FlowGen::PopulateInitialFlows() {
  /* cannot use ctx.current_ns in the master thread... */
  uint64_t now_ns = rdtsc() / tsc_hz * 1e9;

  ScheduleFlow(now_ns);

  if (!quick_rampup_ || flow_pps_ < 1.0 || flow_rate_ < 1.0) {
    return;
  }

  /* emulate pre-existing flows at the beginning */
  double past_origin = MaxFlowPkts() / flow_pps_; /* in secs */
  double step = 1.0 / flow_rate_;

  for (double past = step; past < past_origin; past += step) {
    double pre_consumed_pkts = flow_pps_ * past;
    double flow_pkts = NewFlowPkts();

    if (flow_pkts > pre_consumed_pkts) {
      uint64_t jitter = 1e9 * rng_.GetReal() / flow_pps_;

      struct flow *f = ScheduleFlow(now_ns + jitter);

      /* overwrite with a emulated pre-existing flow */
      f->first_pkt = false;
      f->next_seq_no = 56789;
      f->packets_left = flow_pkts - pre_consumed_pkts;
    }
  }
}

CommandResponse FlowGen::ProcessUpdatableArguments(const bess::pb::FlowGenArg &arg) {

  if (arg.template_().length() == 0) {
    if (strnlen(reinterpret_cast<const char*>(tmpl_), MAX_TEMPLATE_SIZE) == 0) {
      return CommandFailure(EINVAL, "must specify 'template'");
    }
  } else {
    // update template
    if (arg.template_().length() > MAX_TEMPLATE_SIZE) {
      return CommandFailure(EINVAL, "'template' is too big");
    }

    const char *tmpl = arg.template_().c_str();
    const Ethernet *eth = reinterpret_cast<const Ethernet *>(tmpl);
    if (eth->ether_type != be16_t(Ethernet::Type::kIpv4)) {
      return CommandFailure(EINVAL, "'template' is not IPv4");
    }

    const Ipv4 *ip = reinterpret_cast<const Ipv4 *>(eth + 1);
    if (ip->protocol != Ipv4::Proto::kUdp &&
        ip->protocol != Ipv4::Proto::kTcp) {
      return CommandFailure(EINVAL, "'template' is not UDP or TCP");
    }

    if (l4_proto_ == 0) {
      l4_proto_ = ip->protocol;
    } else if (l4_proto_ != ip->protocol) {
      return CommandFailure(EINVAL, "'template' can not be updated");
    }

    template_size_ = arg.template_().length();

    memset(tmpl_, 0, MAX_TEMPLATE_SIZE);
    bess::utils::Copy(tmpl_, tmpl, template_size_);
    CommandResponse err = UpdateBaseAddresses();
    if (err.error().code() != 0) {
      return err;
    }
  }

  if (arg.pps() != 0) {
    if (std::isnan(arg.pps()) || arg.pps() < 0.0) {
      return CommandFailure(EINVAL, "invalid 'pps'");
    }
    total_pps_ = arg.pps();
  }

  if (arg.flow_rate() != 0) {
    if (std::isnan(arg.flow_rate()) || arg.flow_rate() < 0.0) {
      return CommandFailure(EINVAL, "invalid 'flow_rate'");
    }
    flow_rate_ = arg.flow_rate();
  }

  if (flow_rate_ > total_pps_) {
    return CommandFailure(EINVAL, "flow rate cannot be more than pps");
  }

  if (arg.flow_duration() != 0) {
    if (std::isnan(arg.flow_duration()) || arg.flow_duration() < 0.0) {
      return CommandFailure(EINVAL, "invalid 'flow_duration'");
    }
    flow_duration_ = arg.flow_duration();
  }

  if (arg.arrival() == "uniform") {
    arrival_ = Arrival::kUniform;
  } else if (arg.arrival() == "exponential") {
    arrival_ = Arrival::kExponential;
  } else {
    if (arg.arrival() != "") {
      return CommandFailure(
          EINVAL, "'arrival' must be either 'uniform' or 'exponential'");
    }
  }

  if (arg.duration() == "uniform") {
    duration_ = Duration::kUniform;
  } else if (arg.duration() == "pareto") {
    duration_ = Duration::kPareto;
  } else {
    if (arg.duration() != "") {
      return CommandFailure(EINVAL,
                            "'duration' must be either 'uniform' or 'pareto'");
    }
  }

  return CommandSuccess();
}

CommandResponse FlowGen::ProcessArguments(const bess::pb::FlowGenArg &arg) {
  if (arg.quick_rampup()) {
    quick_rampup_ = 1;
  }

  ip_src_range_ = arg.ip_src_range();
  ip_dst_range_ = arg.ip_dst_range();

  if (arg.port_src_range() > 65535 || arg.port_dst_range() > 65535) {
    return CommandFailure(EINVAL, "portrang must be e <= 65535");
  }

  port_src_range_ = (uint16_t)arg.port_src_range();
  port_dst_range_ = (uint16_t)arg.port_dst_range();

  if (ip_src_range_ == 0 && ip_dst_range_ == 0 && port_src_range_ == 0 &&
      port_dst_range_ == 0) {
    /*randomize ports anyway*/
    port_dst_range_ = 20000;
    port_src_range_ = 20000;
  }

  return CommandSuccess();
}

void FlowGen::UpdateDerivedParameters() {
  /* calculate derived variables */
  pareto_.inversed_alpha = 1.0 / pareto_.alpha;

  if (duration_ == Duration::kPareto) {
    MeasureParetoMean();
  }

  concurrent_flows_ = flow_rate_ * flow_duration_;
  if (concurrent_flows_ > 0.0) {
    flow_pps_ = total_pps_ / concurrent_flows_;
  }

  flow_pkts_ = flow_pps_ * flow_duration_;
  if (flow_rate_ > 0.0) {
    flow_gap_ns_ = 1e9 / flow_rate_;
  }
}

CommandResponse FlowGen::CommandUpdate(const bess::pb::FlowGenArg &arg) {
  CommandResponse err;
  err = ProcessUpdatableArguments(arg);
  if (err.error().code() != 0) {
    return err;
  }

  UpdateDerivedParameters();

  return CommandSuccess();
}

CommandResponse FlowGen::CommandSetBurst(
    const bess::pb::FlowGenCommandSetBurstArg &arg) {
  CommandResponse response;

  if (arg.burst() <= bess::PacketBatch::kMaxBurst) {
    burst_ = arg.burst();
  } else {
    return CommandFailure(EINVAL, "'burst' must be no greater than %zu",
                          bess::PacketBatch::kMaxBurst);
  }

  return CommandSuccess();
}

CommandResponse FlowGen::Init(const bess::pb::FlowGenArg &arg) {
  task_id_t tid;
  CommandResponse err;

  rng_.SetSeed(0xBAADF00DDEADBEEFul);

  /* set default parameters */
  total_pps_ = 1000.0;
  flow_rate_ = 10.0;
  flow_duration_ = 10.0;
  arrival_ = Arrival::kUniform;
  duration_ = Duration::kUniform;
  pareto_.alpha = 1.3;
  burst_ = bess::PacketBatch::kMaxBurst;
  l4_proto_ = 0;

  /* register task */
  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID) {
    return CommandFailure(ENOMEM, "task creation failed");
  }

  err = ProcessArguments(arg);
  if (err.error().code() != 0) {
    return err;
  }

  err = ProcessUpdatableArguments(arg);
  if (err.error().code() != 0) {
    return err;
  }

  UpdateDerivedParameters();

  /* add a seed flow (and background flows if necessary) */
  PopulateInitialFlows();

  return CommandSuccess();
}

void FlowGen::DeInit() {
  while (!flows_free_.empty()) {
    delete flows_free_.top();
    flows_free_.pop();
  }
}

CommandResponse FlowGen::UpdateBaseAddresses() {
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(tmpl_ + sizeof(Ethernet));
  ip_src_base_ = ip->src.value();
  ip_dst_base_ = ip->dst.value();
  size_t ip_bytes = (ip->header_length) << 2;

  /* UDP and TCP can share the same header only for  port' contexts */
  Udp *l4 = reinterpret_cast<Udp *>(tmpl_ + sizeof(Ethernet) + ip_bytes);
  port_src_base_ = l4->src_port.value();
  port_dst_base_ = l4->dst_port.value();

  return CommandSuccess();
}

bess::Packet *FlowGen::FillUdpPacket(struct flow *f) {
  bess::Packet *pkt;

  int size = template_size_;

  if (!(pkt = current_worker.packet_pool()->Alloc())) {
    return nullptr;
  }

  char *p = pkt->buffer<char *>() + SNBUF_HEADROOM;
  Ethernet *eth = reinterpret_cast<Ethernet *>(p);
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

  pkt->set_data_off(SNBUF_HEADROOM);
  pkt->set_total_len(size);
  pkt->set_data_len(size);
  bess::utils::Copy(p, tmpl_, size, true);

  ip->src = f->src_ip;
  ip->dst = f->dst_ip;

  size_t ip_bytes = (ip->header_length) << 2;
  Udp *udp = reinterpret_cast<Udp *>(reinterpret_cast<char *>(ip) + ip_bytes);
  udp->src_port = f->src_port;
  udp->dst_port = f->dst_port;

  udp->checksum = bess::utils::CalculateIpv4UdpChecksum(*ip, *udp);
  ip->checksum = bess::utils::CalculateIpv4Checksum(*ip);

  return pkt;
}


bess::Packet *FlowGen::FillTcpPacket(struct flow *f) {
  bess::Packet *pkt;

  int size = template_size_;

  if (!(pkt = current_worker.packet_pool()->Alloc())) {
    return nullptr;
  }

  char *p = pkt->buffer<char *>() + SNBUF_HEADROOM;

  Ethernet *eth = reinterpret_cast<Ethernet *>(p);
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

  bess::utils::Copy(p, tmpl_, size, true);

  // SYN or FIN?
  if (f->first_pkt || f->packets_left <= 1) {
    pkt->set_total_len(60);  // eth + ip + tcp
    pkt->set_data_len(60);   // eth + ip + tcp
    ip->length = be16_t(40);
  } else {
    pkt->set_data_off(SNBUF_HEADROOM);
    pkt->set_total_len(size);
    pkt->set_data_len(size);
  }

  uint8_t tcp_flags = f->first_pkt ? /* SYN */ 0x02 : /* ACK */ 0x10;

  if (f->packets_left <= 1) {
    tcp_flags |= 0x01; /* FIN */
  }

  ip->src = f->src_ip;
  ip->dst = f->dst_ip;

  size_t ip_bytes = (ip->header_length) << 2;
  Tcp *tcp = reinterpret_cast<Tcp *>(reinterpret_cast<char *>(ip) + ip_bytes);
  tcp->src_port = f->src_port;
  tcp->dst_port = f->dst_port;

  tcp->flags = tcp_flags;
  tcp->seq_num = be32_t(f->next_seq_no);
  tcp->checksum = bess::utils::CalculateIpv4TcpChecksum(*ip, *tcp);
  ip->checksum = bess::utils::CalculateIpv4Checksum(*ip);

  f->next_seq_no +=
      f->first_pkt ? 1 : size - (sizeof(*eth) + sizeof(*ip) + sizeof(*tcp));

  return pkt;
}

void FlowGen::GeneratePackets(Context *ctx, bess::PacketBatch *batch) {
  uint64_t now = ctx->current_ns;

  batch->clear();
  const int burst = ACCESS_ONCE(burst_);

  while (batch->cnt() < burst && !events_.empty()) {
    uint64_t t = events_.top().first;
    struct flow *f = events_.top().second;
    if (!f || now < t)
      return;

    events_.pop();

    if (f->packets_left <= 0) {
      flows_free_.push(f);
      active_flows_--;
      continue;
    }

    bess::Packet *pkt = nullptr;
    if (l4_proto_ == Ipv4::Proto::kUdp) {
      pkt = FillUdpPacket(f);
    } else if (l4_proto_ == Ipv4::Proto::kTcp) {
      pkt = FillTcpPacket(f);
    }
    if (pkt) {
      batch->add(pkt);
    }

    if (f->first_pkt) {
      ScheduleFlow(t + NextFlowArrival());
      f->first_pkt = false;
    }

    f->packets_left--;

    events_.emplace(t + static_cast<uint64_t>(1e9 / flow_pps_), f);
  }
}

struct task_result FlowGen::RunTask(Context *ctx, bess::PacketBatch *batch,
                                    void *) {
  if (children_overload_ > 0) {
    return {
        .block = true, .packets = 0, .bits = 0,
    };
  }

  const int pkt_overhead = 24;

  GeneratePackets(ctx, batch);
  RunNextModule(ctx, batch);

  uint32_t cnt = batch->cnt();
  return {.block = (cnt == 0),
          .packets = cnt,
          .bits = ((template_size_ + pkt_overhead) * cnt) * 8};
}

std::string FlowGen::GetDesc() const {
  return bess::utils::Format("%d flows", active_flows_);
}

ADD_MODULE(FlowGen, "flowgen", "generates packets on a flow basis")
