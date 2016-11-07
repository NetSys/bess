#include "flowgen.h"

#include <cmath>
#include <functional>

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

const Commands<Module> FlowGen::cmds = {};
const PbCommands FlowGen::pb_cmds = {};

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

struct snobj *FlowGen::ProcessArguments(struct snobj *arg) {
  struct snobj *t;

  if (!arg || !(t = snobj_eval(arg, "template"))) {
    return snobj_err(EINVAL, "must specify 'template'");
  }

  if (snobj_type(t) != TYPE_BLOB) {
    return snobj_err(EINVAL, "'template' must be BLOB type");
  }

  if (snobj_size(t) > MAX_TEMPLATE_SIZE) {
    return snobj_err(EINVAL, "'template' is too big");
  }

  template_size_ = snobj_size(t);

  memset(templ_, 0, MAX_TEMPLATE_SIZE);
  memcpy(templ_, snobj_blob_get(t), template_size_);

  if ((t = snobj_eval(arg, "pps")) != nullptr) {
    total_pps_ = snobj_number_get(t);
    if (std::isnan(total_pps_) || total_pps_ < 0.0) {
      return snobj_err(EINVAL, "invalid 'pps'");
    }
  }

  if ((t = snobj_eval(arg, "flow_rate")) != nullptr) {
    flow_rate_ = snobj_number_get(t);
    if (std::isnan(flow_rate_) || flow_rate_ < 0.0) {
      return snobj_err(EINVAL, "invalid 'flow_rate'");
    }
  }

  if ((t = snobj_eval(arg, "flow_duration")) != nullptr) {
    flow_duration_ = snobj_number_get(t);
    if (std::isnan(flow_duration_) || flow_duration_ < 0.0) {
      return snobj_err(EINVAL, "invalid 'flow_duration'");
    }
  }

  if ((t = snobj_eval(arg, "arrival")) != nullptr) {
    if (strcmp(snobj_str_get(t), "uniform") == 0) {
      arrival_ = ARRIVAL_UNIFORM;
    } else if (strcmp(snobj_str_get(t), "exponential") == 0) {
      arrival_ = ARRIVAL_EXPONENTIAL;
    } else {
      return snobj_err(EINVAL,
                       "'arrival' must be either "
                       "'uniform' or 'exponential'");
    }
  }

  if ((t = snobj_eval(arg, "duration")) != nullptr) {
    if (strcmp(snobj_str_get(t), "uniform") == 0) {
      duration_ = DURATION_UNIFORM;
    } else if (strcmp(snobj_str_get(t), "pareto") == 0) {
      duration_ = DURATION_PARETO;
    } else {
      return snobj_err(EINVAL,
                       "'duration' must be either "
                       "'uniform' or 'pareto'");
    }
  }

  if (snobj_eval_int(arg, "quick_rampup")) {
    quick_rampup_ = 1;
  }

  return nullptr;
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

  if (arg.arrival() == bess::pb::FlowGenArg::UNIFORM) {
    arrival_ = ARRIVAL_UNIFORM;
  } else if (arg.arrival() == bess::pb::FlowGenArg::EXPONENTIAL) {
    arrival_ = ARRIVAL_EXPONENTIAL;
  } else {
    return pb_error(EINVAL,
                    "'arrival' must be either "
                    "'uniform' or 'exponential'");
  }

  if (arg.duration() == bess::pb::FlowGenArg::UNIFORM) {
    duration_ = DURATION_UNIFORM;
  } else if (arg.duration() == bess::pb::FlowGenArg::PARETO) {
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

pb_error_t FlowGen::InitPb(const bess::pb::FlowGenArg &arg) {
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

struct snobj *FlowGen::InitFlowPoolOld() {
  /* allocate 20% more in case of temporal overflow */
  allocated_flows_ = (int)(concurrent_flows_ * 1.2);
  if (allocated_flows_ < 128) {
    allocated_flows_ = 128;
  }

  flows_ = static_cast<struct flow *>(
      mem_alloc(allocated_flows_ * sizeof(struct flow)));
  if (!flows_) {
    return snobj_err(ENOMEM, "memory allocation failed (%d flows)",
                     allocated_flows_);
  }

  cdlist_head_init(&flows_free_);

  for (int i = 0; i < allocated_flows_; i++) {
    struct flow *f = &flows_[i];
    cdlist_add_tail(&flows_free_, &f->free);
  }

  return nullptr;
}

struct snobj *FlowGen::Init(struct snobj *arg) {
  task_id_t tid;
  struct snobj *err;

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
  if (tid == INVALID_TASK_ID)
    return snobj_err(ENOMEM, "task creation failed");

  templ_ = new char[MAX_TEMPLATE_SIZE];
  if (templ_ == nullptr) {
    return snobj_err(ENOMEM, "unable to allocate template");
  }

  err = ProcessArguments(arg);
  if (err) {
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
  err = InitFlowPoolOld();
  if (err) {
    return err;
  }

  /* initialize time-sorted priority queue */
  events_ = EventQueue(EventLess);

  /* add a seed flow (and background flows if necessary) */
  PopulateInitialFlows();

  return nullptr;
}

void FlowGen::Deinit() {
  mem_free(flows_);
  delete templ_;
}

struct snbuf *FlowGen::FillPacket(struct flow *f) {
  struct snbuf *pkt;
  char *p;

  uint8_t tcp_flags;

  int size = template_size_;

  if (!(pkt = snb_alloc())) {
    return nullptr;
  }

  p = reinterpret_cast<char *>(pkt->mbuf.buf_addr) +
      static_cast<size_t>(SNBUF_HEADROOM);
  if (!p) {
    return nullptr;
  }

  pkt->mbuf.data_off = SNBUF_HEADROOM;
  pkt->mbuf.pkt_len = size;
  pkt->mbuf.data_len = size;

  memcpy_sloppy(p, templ_, size);

  tcp_flags = f->first ? /* SYN */ 0x02 : /* ACK */ 0x10;

  if (f->packets_left <= 1)
    tcp_flags |= 0x01; /* FIN */

  *(uint32_t *)(p + 14 + /* IP dst */ 16) = f->flow_id;
  *(uint8_t *)(p + 14 + /* IP */ 20 + /* TCP flags */ 13) = tcp_flags;

  return pkt;
}

void FlowGen::GeneratePackets(struct pkt_batch *batch) {
  uint64_t now = ctx.current_ns();

  batch_clear(batch);

  while (!batch_full(batch)) {
    uint64_t t;
    struct flow *f;
    struct snbuf *pkt;

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
      batch_add(batch, pkt);
    }
  }
}

struct task_result FlowGen::RunTask(void *) {
  struct pkt_batch batch;
  struct task_result ret;

  const int pkt_overhead = 24;

  GeneratePackets(&batch);
  if (batch.cnt > 0)
    RunNextModule(&batch);

  ret = (struct task_result){
      .packets = static_cast<uint64_t>(batch.cnt),
      .bits = static_cast<uint64_t>(
          ((template_size_ + pkt_overhead) * batch.cnt) * 8),
  };

  return ret;
}

std::string FlowGen::GetDesc() const {
  return bess::utils::Format("%d flows", active_flows_);
}

struct snobj *FlowGen::GetDump() const {
  struct snobj *r = snobj_map();

  {
    struct snobj *t = snobj_map();

    snobj_map_set(t, "allocated_flows", snobj_int(allocated_flows_));
    snobj_map_set(t, "active_flows", snobj_int(active_flows_));
    snobj_map_set(t, "generated_flows", snobj_int(generated_flows_));

    snobj_map_set(r, "stats", t);
  }

  {
    struct snobj *t = snobj_map();

    snobj_map_set(t, "total_pps", snobj_double(total_pps_));
    snobj_map_set(t, "flow_rate", snobj_double(flow_rate_));
    snobj_map_set(t, "flow_duration", snobj_double(flow_duration_));

    snobj_map_set(r, "load", t);
  }

  {
    struct snobj *t = snobj_map();

    snobj_map_set(t, "concurrent_flows", snobj_double(concurrent_flows_));
    snobj_map_set(t, "flow_pps", snobj_double(flow_pps_));
    snobj_map_set(t, "flow_pkts", snobj_double(flow_pkts_));
    snobj_map_set(t, "flow_gap_ns", snobj_double(flow_gap_ns_));

    snobj_map_set(r, "derived", t);
  }

  {
    struct snobj *t = snobj_map();

    snobj_map_set(t, "quick_rampup", snobj_int(quick_rampup_));
    snobj_map_set(
        t, "arrival",
        snobj_str(arrival_ == ARRIVAL_UNIFORM ? "uniform" : "exponential"));
    snobj_map_set(
        t, "duration",
        snobj_str(duration_ == DURATION_UNIFORM ? "uniform" : "pareto"));

    snobj_map_set(r, "behavior", t);
  }

  if (duration_ == DURATION_PARETO) {
    struct snobj *t = snobj_map();

    snobj_map_set(t, "alpha", snobj_double(pareto_.alpha));
    snobj_map_set(t, "mean", snobj_double(pareto_.mean));
    snobj_map_set(t, "max", snobj_int(MaxFlowPkts()));

    snobj_map_set(r, "pareto", t);
  }

  return r;
}

ADD_MODULE(FlowGen, "flowgen", "generates packets on a flow basis")
