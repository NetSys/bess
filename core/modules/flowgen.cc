#include "flowgen.h"

#include <cmath>
#include <functional>

#include "../mem_alloc.h"
#include "../utils/format.h"
#include "../utils/time.h"

#define MAX_TEMPLATE_SIZE 1536

#define RETRY_NS 1000000ul /* 1 ms */

struct flow {
  uint32_t flow_id;
  int packets_left;
  int first;
  struct cdlist_item free;
};

typedef std::pair<uint64_t, struct flow *> Event;
typedef std::priority_queue<Event, std::vector<Event>,
                            std::function<bool(Event, Event)>>
    EventQueue;

bool EventLess(const Event &a, const Event &b) {
  return a.first < b.first;
}

/* we ignore the last 1% tail to make the variance finite */
const double PARETO_TAIL_LIMIT = 0.99;

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
    case DURATION_UNIFORM:
      return flow_pkts_;
    case DURATION_PARETO:
      return scaled_pareto_variate(pareto_.inversed_alpha, pareto_.mean,
                                   flow_pkts_, rng_.GetReal());
    default:
      assert(0);
  }
}

inline double FlowGen::MaxFlowPkts() const {
  switch (duration_) {
    case DURATION_UNIFORM:
      return flow_pkts_;
    case DURATION_PARETO:
      return scaled_pareto_variate(pareto_.inversed_alpha, pareto_.mean,
                                   flow_pkts_, 1.0);
    default:
      assert(0);
  }
}

inline uint64_t FlowGen::NextFlowArrival() {
  switch (arrival_) {
    case ARRIVAL_UNIFORM:
      return flow_gap_ns_;
      break;
    case ARRIVAL_EXPONENTIAL:
      return -log(rng_.GetRealNonzero()) * flow_gap_ns_;
      break;
    default:
      assert(0);
  }
}

inline struct flow *FlowGen::ScheduleFlow(uint64_t time_ns) {
  struct cdlist_item *item;
  struct flow *f;

  item = cdlist_pop_head(&flows_free_);
  if (!item) {
    return nullptr;
  }

  f = container_of(item, struct flow, free);
  f->first = 1;
  f->flow_id = rng_.Get();

  /* compensate the fraction part by adding [0.0, 1.0) */
  f->packets_left = NewFlowPkts() + rng_.GetReal();

  active_flows_++;
  generated_flows_++;

  events_.push(Event(time_ns, f));

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
  struct flow *f;

  f = ScheduleFlow(now_ns);
  assert(f);

  if (!quick_rampup_) {
    return;
  }

  if (flow_pps_ < 1.0 || flow_rate_ < 1.0) {
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

      f = ScheduleFlow(now_ns + jitter);
      if (!f) {
        break;
      }

      /* overwrite with a emulated pre-existing flow */
      f->first = 0;
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
    arrival_ = ARRIVAL_UNIFORM;
  } else if (arg.arrival() == "exponential") {
    arrival_ = ARRIVAL_EXPONENTIAL;
  } else {
    return pb_error(EINVAL,
                    "'arrival' must be either "
                    "'uniform' or 'exponential'");
  }

  if (arg.duration() == "uniform") {
    duration_ = DURATION_UNIFORM;
  } else if (arg.duration() == "pareto") {
    duration_ = DURATION_PARETO;
  } else {
    return pb_error(EINVAL,
                    "'duration' must be either "
                    "'uniform' or 'pareto'");
  }

  if (arg.quick_rampup()) {
    quick_rampup_ = 1;
  }

  return pb_errno(0);
}

pb_error_t FlowGen::InitFlowPool() {
  /* allocate 20% more in case of temporal overflow */
  allocated_flows_ = (int)(concurrent_flows_ * 1.2);
  if (allocated_flows_ < 128) {
    allocated_flows_ = 128;
  }

  flows_ = static_cast<struct flow *>(
      mem_alloc(allocated_flows_ * sizeof(struct flow)));
  if (!flows_) {
    return pb_error(ENOMEM, "memory allocation failed (%d flows)",
                    allocated_flows_);
  }

  cdlist_head_init(&flows_free_);

  for (int i = 0; i < allocated_flows_; i++) {
    struct flow *f = &flows_[i];
    cdlist_add_tail(&flows_free_, &f->free);
  }

  return pb_errno(0);
}

pb_error_t FlowGen::Init(const bess::pb::FlowGenArg &arg) {
  task_id_t tid;
  pb_error_t err;

  rng_.SetSeed(0xBAADF00DDEADBEEFul);

  /* set default parameters */
  total_pps_ = 1000.0;
  flow_rate_ = 10.0;
  flow_duration_ = 10.0;
  arrival_ = ARRIVAL_UNIFORM;
  duration_ = DURATION_UNIFORM;
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

  /* calculate derived variables */
  pareto_.inversed_alpha = 1.0 / pareto_.alpha;

  if (duration_ == DURATION_PARETO) {
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

  /* initialize flow pool */
  err = InitFlowPool();
  if (err.err() != 0) {
    return err;
  }

  /* initialize time-sorted priority queue */
  events_ = EventQueue(EventLess);

  /* add a seed flow (and background flows if necessary) */
  PopulateInitialFlows();

  return pb_errno(0);
}

void FlowGen::Deinit() {
  mem_free(flows_);
  delete templ_;
}

bess::Packet *FlowGen::FillPacket(struct flow *f) {
  bess::Packet *pkt;
  char *p;

  uint8_t tcp_flags;

  int size = template_size_;

  if (!(pkt = bess::Packet::Alloc())) {
    return nullptr;
  }

  p = reinterpret_cast<char *>(pkt->buffer()) +
      static_cast<size_t>(SNBUF_HEADROOM);
  if (!p) {
    return nullptr;
  }

  pkt->set_data_off(SNBUF_HEADROOM);
  pkt->set_total_len(size);
  pkt->set_data_len(size);

  memcpy_sloppy(p, templ_, size);

  tcp_flags = f->first ? /* SYN */ 0x02 : /* ACK */ 0x10;

  if (f->packets_left <= 1)
    tcp_flags |= 0x01; /* FIN */

  *(uint32_t *)(p + 14 + /* IP dst */ 16) = f->flow_id;
  *(uint8_t *)(p + 14 + /* IP */ 20 + /* TCP flags */ 13) = tcp_flags;

  return pkt;
}

void FlowGen::GeneratePackets(bess::PacketBatch *batch) {
  uint64_t now = ctx.current_ns();

  batch->clear();

  while (!batch->full()) {
    uint64_t t;
    struct flow *f;
    bess::Packet *pkt;

    t = events_.top().first;
    f = events_.top().second;
    if (!f || now < t)
      return;

    events_.pop();

    if (f->packets_left <= 0) {
      cdlist_add_head(&flows_free_, &f->free);
      active_flows_--;
      continue;
    }

    pkt = FillPacket(f);

    if (f->first) {
      uint64_t delay_ns = NextFlowArrival();
      struct flow *new_f;

      new_f = ScheduleFlow(t + delay_ns);
      if (!new_f) {
        /* temporarily out of free flow data. retry. */
        events_.push(std::pair<uint64_t, struct flow *>(t + RETRY_NS, f));
        continue;
      }

      f->first = 0;
    }

    f->packets_left--;

    events_.push(
        std::pair<uint64_t, struct flow *>(t + (uint64_t)(1e9 / flow_pps_), f));

    if (pkt) {
      batch->add(pkt);
    }
  }
}

struct task_result FlowGen::RunTask(void *) {
  bess::PacketBatch batch;
  struct task_result ret;

  const int pkt_overhead = 24;

  GeneratePackets(&batch);
  if (!batch.empty())
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
