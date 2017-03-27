#include "flowgen.h"

#include <arpa/inet.h>
#include <cmath>
#include <functional>

#include "../mem_alloc.h"
#include "../utils/ether.h"
#include "../utils/format.h"
#include "../utils/ip.h"
#include "../utils/tcp.h"
#include "../utils/time.h"

using bess::utils::EthHeader;
using bess::utils::Ipv4Header;
using bess::utils::TcpHeader;

#define MAX_TEMPLATE_SIZE 1536

/* we ignore the last 1% tail to make the variance finite */
const double PARETO_TAIL_LIMIT = 0.99;

const Commands FlowGen::cmds = {
    {"update", "FlowGenArg", MODULE_CMD_FUNC(&FlowGen::CommandUpdate), 0}};

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
  f->src_ip = htonl(ip_src_base_ + rng_.GetRange(ip_src_range_));
  f->dst_ip = htonl(ip_dst_base_ + rng_.GetRange(ip_dst_range_));
  f->src_port = htons(port_src_base_ + rng_.GetRange(port_src_range_));
  f->dst_port = htons(port_src_base_ + rng_.GetRange(port_dst_range_));

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

pb_error_t FlowGen::ProcessArguments(const bess::pb::FlowGenArg &arg) {
  if (arg.template_().length() == 0) {
    return pb_error(EINVAL, "must specify 'template'");
  }

  if (arg.template_().length() > MAX_TEMPLATE_SIZE) {
    return pb_error(EINVAL, "'template' is too big");
  }

  template_size_ = arg.template_().length();

  memset(templ_, 0, MAX_TEMPLATE_SIZE);
  memcpy(templ_, arg.template_().c_str(), template_size_);
  pb_error_t err = UpdateBaseAddresses();
  if (err.err() != 0) {
    return err;
  }

  total_pps_ = arg.pps();
  if (std::isnan(total_pps_) || total_pps_ < 0.0) {
    return pb_error(EINVAL, "invalid 'pps'");
  }

  flow_rate_ = arg.flow_rate();
  if (std::isnan(flow_rate_) || flow_rate_ < 0.0) {
    return pb_error(EINVAL, "invalid 'flow_rate'");
  }

  flow_duration_ = arg.flow_duration();
  if (std::isnan(flow_duration_) || flow_duration_ < 0.0) {
    return pb_error(EINVAL, "invalid 'flow_duration'");
  }

  if (arg.arrival() == "uniform") {
    arrival_ = Arrival::kUniform;
  } else if (arg.arrival() == "exponential") {
    arrival_ = Arrival::kExponential;
  } else {
    return pb_error(EINVAL,
                    "'arrival' must be either 'uniform' or 'exponential'");
  }

  if (arg.duration() == "uniform") {
    duration_ = Duration::kUniform;
  } else if (arg.duration() == "pareto") {
    duration_ = Duration::kPareto;
  } else {
    return pb_error(EINVAL, "'duration' must be either 'uniform' or 'pareto'");
  }

  if (arg.quick_rampup()) {
    quick_rampup_ = 1;
  }

  ip_src_range_ = arg.ip_src_range();
  ip_dst_range_ = arg.ip_dst_range();

  if (arg.port_src_range() > 65535 || arg.port_dst_range() > 65535) {
    return pb_error(EINVAL, "portrang must be e <= 65535");
  }

  port_src_range_ = (uint16_t)arg.port_src_range();
  port_dst_range_ = (uint16_t)arg.port_dst_range();

  if (ip_src_range_ == 0 && ip_dst_range_ == 0 && port_src_range_ == 0 &&
      port_dst_range_ == 0) {
    /*randomize ports anyway*/
    port_dst_range_ = 20000;
    port_src_range_ = 20000;
  }

  return pb_errno(0);
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

pb_cmd_response_t FlowGen::CommandUpdate(const bess::pb::FlowGenArg &arg) {
  pb_cmd_response_t response;

  if (arg.template_().length() > 0) {
    LOG(INFO) << "Updating FlowGen template";
    if (arg.template_().length() > MAX_TEMPLATE_SIZE) {
      set_cmd_response_error(&response,
                             pb_error(EINVAL, "'template' is too big"));
      return response;
    }
    template_size_ = arg.template_().length();

    memset(templ_, 0, MAX_TEMPLATE_SIZE);
    memcpy(templ_, arg.template_().c_str(), template_size_);
  }

  double prev_pps = 0;
  if (!(std::isnan(arg.pps()) || arg.pps() == 0.0)) {
    if (arg.pps() < 0) {
      set_cmd_response_error(&response,
                             pb_error(EINVAL, "pps cannot be negative"));
      return response;
    }

    LOG(INFO) << "Updating FlowGen pps" << arg.pps();
    prev_pps = total_pps_;
    total_pps_ = arg.pps();
  }

  double prev_flowrate = 0;
  if (!(std::isnan(arg.flow_rate()) || arg.flow_rate() == 0.0)) {
    if (arg.flow_rate() < 0) {
      set_cmd_response_error(&response,
                             pb_error(EINVAL, "flow rate cannot be negative"));
      return response;
    }

    LOG(INFO) << "Updating FlowGen flow rate" << arg.flow_rate();

    prev_flowrate = flow_rate_;
    flow_rate_ = arg.flow_rate();
  }

  if (flow_rate_ > total_pps_) {
    flow_rate_ = prev_flowrate;
    total_pps_ = prev_pps;
    set_cmd_response_error(
        &response, pb_error(EINVAL, "flow rate cannot be more than pps"));
    return response;
  }

  if (!(std::isnan(arg.flow_duration()) || arg.flow_duration() == 0.0)) {
    if (arg.flow_duration() < 0) {
      set_cmd_response_error(
          &response, pb_error(EINVAL, "flow duration cannot be negative"));
      return response;
    }

    LOG(INFO) << "Updating FlowGen flow duration" << arg.flow_duration();
    flow_duration_ = arg.flow_duration();
  }

  if (arg.arrival() == "uniform") {
    arrival_ = Arrival::kUniform;
  } else if (arg.arrival() == "exponential") {
    arrival_ = Arrival::kExponential;
  } else if (arg.arrival() != "") {
    set_cmd_response_error(
        &response,
        pb_error(EINVAL, "arrival must be 'uniform' or 'exponential'"));
    return response;
  }

  if (arg.duration() == "uniform") {
    duration_ = Duration::kUniform;
  } else if (arg.duration() == "pareto") {
    duration_ = Duration::kPareto;
  } else if (arg.duration() != "") {
    set_cmd_response_error(
        &response,
        pb_error(EINVAL, "duration must be 'uniform' or 'pareto' {%s}",
                 arg.duration().c_str()));
    return response;
  }

  UpdateDerivedParameters();

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

pb_error_t FlowGen::Init(const bess::pb::FlowGenArg &arg) {
  task_id_t tid;
  pb_error_t err;

  rng_.SetSeed(0xBAADF00DDEADBEEFul);

  /* set default parameters */
  total_pps_ = 1000.0;
  flow_rate_ = 10.0;
  flow_duration_ = 10.0;
  arrival_ = Arrival::kUniform;
  duration_ = Duration::kUniform;
  pareto_.alpha = 1.3;

  /* register task */
  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID) {
    return pb_error(ENOMEM, "task creation failed");
  }

  templ_ = new char[MAX_TEMPLATE_SIZE];
  if (templ_ == nullptr) {
    return pb_error(ENOMEM, "unable to allocate template");
  }

  err = ProcessArguments(arg);
  if (err.err() != 0) {
    return err;
  }

  UpdateDerivedParameters();

  /* add a seed flow (and background flows if necessary) */
  PopulateInitialFlows();

  return pb_errno(0);
}

void FlowGen::DeInit() {
  while (!flows_free_.empty()) {
    delete flows_free_.top();
    flows_free_.pop();
  }

  delete templ_;
}

pb_error_t FlowGen::UpdateBaseAddresses() {
  char *p = reinterpret_cast<char *>(templ_);
  if (!p) {
    return pb_error(EINVAL, "must specify 'template'");
  }

  Ipv4Header *ipheader = reinterpret_cast<Ipv4Header *>(p + sizeof(EthHeader));
  TcpHeader *tcpheader =
      reinterpret_cast<TcpHeader *>(p + sizeof(EthHeader) + sizeof(Ipv4Header));

  ip_src_base_ = ntohl(ipheader->src);
  ip_dst_base_ = ntohl(ipheader->dst);
  port_src_base_ = ntohs(tcpheader->src_port);
  port_dst_base_ = ntohs(tcpheader->dst_port);
  return pb_errno(0);
}

bess::Packet *FlowGen::FillPacket(struct flow *f) {
  bess::Packet *pkt;

  int size = template_size_;

  if (!(pkt = bess::Packet::Alloc())) {
    return nullptr;
  }

  char *p = reinterpret_cast<char *>(pkt->buffer()) +
            static_cast<size_t>(SNBUF_HEADROOM);
  if (!p) {
    return nullptr;
  }

  EthHeader *eth = reinterpret_cast<EthHeader *>(p);
  Ipv4Header *ip = reinterpret_cast<Ipv4Header *>(eth + 1);
  TcpHeader *tcp = reinterpret_cast<TcpHeader *>(ip + 1);

  // SYN or FIN?
  if (f->first_pkt || f->packets_left <= 1) {
    pkt->set_total_len(60);  // eth + ip + tcp
    pkt->set_data_len(60);   // eth + ip + tcp
    ip->length = htons(40);
  } else {
    pkt->set_data_off(SNBUF_HEADROOM);
    pkt->set_total_len(size);
    pkt->set_data_len(size);

    rte_memcpy(p, templ_, size);
  }

  uint8_t tcp_flags = f->first_pkt ? /* SYN */ 0x02 : /* ACK */ 0x10;

  if (f->packets_left <= 1) {
    tcp_flags |= 0x01; /* FIN */
  }

  ip->src = f->src_ip;
  ip->dst = f->dst_ip;
  tcp->src_port = f->src_port;
  tcp->dst_port = f->dst_port;

  tcp->flags = tcp_flags;
  tcp->seq_num = htonl(f->next_seq_no);

  f->next_seq_no +=
      f->first_pkt ? 1 : size - (sizeof(*eth) + sizeof(*ip) + sizeof(*tcp));

  return pkt;
}

void FlowGen::GeneratePackets(bess::PacketBatch *batch) {
  uint64_t now = ctx.current_ns();

  batch->clear();

  while (!batch->full() && !events_.empty()) {
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

    bess::Packet *pkt = FillPacket(f);
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

struct task_result FlowGen::RunTask(void *) {
  bess::PacketBatch batch;
  struct task_result ret;

  const int pkt_overhead = 24;

  GeneratePackets(&batch);
  RunNextModule(&batch);

  ret = (struct task_result){
      .packets = static_cast<uint64_t>(batch.cnt()),
      .bits = static_cast<uint64_t>(
          ((template_size_ + pkt_overhead) * batch.cnt()) * 8),
  };

  return ret;
}

std::string FlowGen::GetDesc() const {
  return bess::utils::Format("%d flows", active_flows_);
}

ADD_MODULE(FlowGen, "flowgen", "generates packets on a flow basis")
