#include <math.h>
#include <functional>
#include <queue>

#include "../time.h"
#include "../utils/random.h"

#include "../module.h"

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

bool EventLess(const Event &a, const Event &b) { return a.first < b.first; }

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

class FlowGen : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void Deinit();

  virtual struct task_result RunTask(void *arg);

  virtual struct snobj *GetDesc();
  virtual struct snobj *GetDump();

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 1;

 private:
  int active_flows;
  int allocated_flows;
  uint64_t generated_flows;
  struct flow *flows;
  struct cdlist_head flows_free;

  EventQueue events;

  char templ[MAX_TEMPLATE_SIZE];
  int template_size;

  uint64_t rseed;

  /* behavior parameters */
  int quick_rampup;

  enum {
    ARRIVAL_UNIFORM = 0,
    ARRIVAL_EXPONENTIAL,
  } arrival;

  enum {
    DURATION_UNIFORM = 0,
    DURATION_PARETO,
  } duration;

  /* load parameters */
  double total_pps;
  double flow_rate;     /* in flows/s */
  double flow_duration; /* in seconds */

  /* derived variables */
  double concurrent_flows; /* expected # of flows */
  double flow_pps;         /* packets/s/flow */
  double flow_pkts;        /* flow_pps * flow_duration */
  double flow_gap_ns;      /* == 10^9 / flow_rate */

  struct {
    double alpha;
    double inversed_alpha; /* 1.0 / alpha */
    double mean;           /* determined by alpha */
  } pareto;

  inline double NewFlowPkts();
  inline double MaxFlowPkts();
  inline uint64_t NextFlowArrival();
  inline struct flow *ScheduleFlow(uint64_t time_ns);
  void MeasureParetoMean();
  void PopulateInitialFlows();
  struct snbuf *FillPacket(struct flow *f);
  void GeneratePackets(struct pkt_batch *batch);
  struct snobj *InitFlowPool();
  struct snobj *ProcessArguments(struct snobj *arg);
};

inline double FlowGen::NewFlowPkts() {
  switch (this->duration) {
    case DURATION_UNIFORM:
      return this->flow_pkts;
    case DURATION_PARETO:
      return scaled_pareto_variate(this->pareto.inversed_alpha,
                                   this->pareto.mean, this->flow_pkts,
                                   rand_fast_real(&this->rseed));
    default:
      assert(0);
  }
}

inline double FlowGen::MaxFlowPkts() {
  switch (this->duration) {
    case DURATION_UNIFORM:
      return this->flow_pkts;
    case DURATION_PARETO:
      return scaled_pareto_variate(this->pareto.inversed_alpha,
                                   this->pareto.mean, this->flow_pkts, 1.0);
    default:
      assert(0);
  }
}

inline uint64_t FlowGen::NextFlowArrival() {
  switch (this->arrival) {
    case ARRIVAL_UNIFORM:
      return this->flow_gap_ns;
      break;
    case ARRIVAL_EXPONENTIAL:
      return -log(rand_fast_real2(&this->rseed)) * this->flow_gap_ns;
      break;
    default:
      assert(0);
  }
}

inline struct flow *FlowGen::ScheduleFlow(uint64_t time_ns) {
  struct cdlist_item *item;
  struct flow *f;

  item = cdlist_pop_head(&this->flows_free);
  if (!item) return NULL;

  f = container_of(item, struct flow, free);
  f->first = 1;
  f->flow_id = (uint32_t)rand_fast(&this->rseed);

  /* compensate the fraction part by adding [0.0, 1.0) */
  f->packets_left = this->NewFlowPkts() + rand_fast_real(&this->rseed);
  ;

  this->active_flows++;
  this->generated_flows++;

  this->events.push(Event(time_ns, f));

  return f;
}

void FlowGen::MeasureParetoMean() {
  const int iteration = 1000000;
  double total = 0.0;

  for (int i = 0; i <= iteration; i++) {
    double y = i / (double)iteration;
    double x = pareto_variate(this->pareto.inversed_alpha, y);
    total += x;
  }

  this->pareto.mean = total / (iteration + 1);
}

void FlowGen::PopulateInitialFlows() {
  /* cannot use ctx.current_ns in the master thread... */
  uint64_t now_ns = rdtsc() / tsc_hz * 1e9;
  struct flow *f;

  f = this->ScheduleFlow(now_ns);
  assert(f);

  if (!this->quick_rampup) return;

  if (this->flow_pps < 1.0 || this->flow_rate < 1.0) return;

  /* emulate pre-existing flows at the beginning */
  double past_origin = this->MaxFlowPkts() / this->flow_pps; /* in secs */
  double step = 1.0 / this->flow_rate;

  for (double past = step; past < past_origin; past += step) {
    double pre_consumed_pkts = this->flow_pps * past;
    double flow_pkts = this->NewFlowPkts();

    if (flow_pkts > pre_consumed_pkts) {
      uint64_t jitter = 1e9 * rand_fast_real(&this->rseed) / this->flow_pps;

      f = this->ScheduleFlow(now_ns + jitter);
      if (!f) break;

      /* overwrite with a emulated pre-existing flow */
      f->first = 0;
      f->packets_left = flow_pkts - pre_consumed_pkts;
    }
  }
}

struct snobj *FlowGen::ProcessArguments(struct snobj *arg) {
  struct snobj *t;

  if (!arg || !(t = snobj_eval(arg, "template")))
    return snobj_err(EINVAL, "must specify 'template'");

  if (snobj_type(t) != TYPE_BLOB)
    return snobj_err(EINVAL, "'template' must be BLOB type");

  if (snobj_size(t) > MAX_TEMPLATE_SIZE)
    return snobj_err(EINVAL, "'template' is too big");

  this->template_size = snobj_size(t);

  memset(this->templ, 0, MAX_TEMPLATE_SIZE);
  memcpy(this->templ, snobj_blob_get(t), this->template_size);

  if ((t = snobj_eval(arg, "pps")) != NULL) {
    this->total_pps = snobj_number_get(t);
    if (isnan(this->total_pps) || this->total_pps < 0.0)
      return snobj_err(EINVAL, "invalid 'pps'");
  }

  if ((t = snobj_eval(arg, "flow_rate")) != NULL) {
    this->flow_rate = snobj_number_get(t);
    if (isnan(this->flow_rate) || this->flow_rate < 0.0)
      return snobj_err(EINVAL, "invalid 'flow_rate'");
  }

  if ((t = snobj_eval(arg, "flow_duration")) != NULL) {
    this->flow_duration = snobj_number_get(t);
    if (isnan(this->flow_duration) || this->flow_duration < 0.0)
      return snobj_err(EINVAL, "invalid 'flow_duration'");
  }

  if ((t = snobj_eval(arg, "arrival")) != NULL) {
    if (strcmp(snobj_str_get(t), "uniform") == 0)
      this->arrival = ARRIVAL_UNIFORM;
    else if (strcmp(snobj_str_get(t), "exponential") == 0)
      this->arrival = ARRIVAL_EXPONENTIAL;
    else
      return snobj_err(EINVAL,
                       "'arrival' must be either "
                       "'uniform' or 'exponential'");
  }

  if ((t = snobj_eval(arg, "duration")) != NULL) {
    if (strcmp(snobj_str_get(t), "uniform") == 0)
      this->duration = DURATION_UNIFORM;
    else if (strcmp(snobj_str_get(t), "pareto") == 0)
      this->duration = DURATION_PARETO;
    else
      return snobj_err(EINVAL,
                       "'duration' must be either "
                       "'uniform' or 'pareto'");
  }

  if (snobj_eval_int(arg, "quick_rampup")) this->quick_rampup = 1;

  return NULL;
}

struct snobj *FlowGen::InitFlowPool() {
  /* allocate 20% more in case of temporal overflow */
  this->allocated_flows = (int)(this->concurrent_flows * 1.2);
  if (this->allocated_flows < 128) this->allocated_flows = 128;

  this->flows = static_cast<struct flow *>(
      mem_alloc(this->allocated_flows * sizeof(struct flow)));
  if (!this->flows)
    return snobj_err(ENOMEM, "memory allocation failed (%d flows)",
                     this->allocated_flows);

  cdlist_head_init(&this->flows_free);

  for (int i = 0; i < this->allocated_flows; i++) {
    struct flow *f = &this->flows[i];
    cdlist_add_tail(&this->flows_free, &f->free);
  }

  return NULL;
}

struct snobj *FlowGen::Init(struct snobj *arg) {
  task_id_t tid;
  struct snobj *err;

  this->rseed = 0xBAADF00DDEADBEEFul;

  /* set default parameters */
  this->total_pps = 1000.0;
  this->flow_rate = 10.0;
  this->flow_duration = 10.0;
  this->arrival = ARRIVAL_UNIFORM;
  this->duration = DURATION_UNIFORM;
  this->pareto.alpha = 1.3;

  /* register task */
  tid = register_task(this, NULL);
  if (tid == INVALID_TASK_ID) return snobj_err(ENOMEM, "task creation failed");

  err = this->ProcessArguments(arg);
  if (err) return err;

  /* calculate derived variables */
  this->pareto.inversed_alpha = 1.0 / this->pareto.alpha;

  if (this->duration == DURATION_PARETO) this->MeasureParetoMean();

  this->concurrent_flows = this->flow_rate * this->flow_duration;
  if (this->concurrent_flows > 0.0)
    this->flow_pps = this->total_pps / this->concurrent_flows;

  this->flow_pkts = this->flow_pps * this->flow_duration;
  if (this->flow_rate > 0.0) this->flow_gap_ns = 1e9 / this->flow_rate;

  /* initialize flow pool */
  err = this->InitFlowPool();
  if (err) return err;

  /* initialize time-sorted priority queue */
  this->events = EventQueue(EventLess);

  /* add a seed flow (and background flows if necessary) */
  this->PopulateInitialFlows();

  return NULL;
}

void FlowGen::Deinit() { mem_free(this->flows); }

struct snbuf *FlowGen::FillPacket(struct flow *f) {
  struct snbuf *pkt;
  char *p;

  uint8_t tcp_flags;

  int size = this->template_size;

  if (!(pkt = snb_alloc())) return NULL;

  p = reinterpret_cast<char *>(pkt->mbuf.buf_addr) +
      static_cast<size_t>(SNBUF_HEADROOM);

  pkt->mbuf.data_off = static_cast<size_t>(SNBUF_HEADROOM);
  pkt->mbuf.pkt_len = size;
  pkt->mbuf.data_len = size;

  memcpy_sloppy(p, this->templ, size);

  tcp_flags = f->first ? /* SYN */ 0x02 : /* ACK */ 0x10;

  if (f->packets_left <= 1) tcp_flags |= 0x01; /* FIN */

  *(uint32_t *)(p + 14 + /* IP dst */ 16) = f->flow_id;
  *(uint8_t *)(p + 14 + /* IP */ 20 + /* TCP flags */ 13) = tcp_flags;

  return pkt;
}

void FlowGen::GeneratePackets(struct pkt_batch *batch) {
  uint64_t now = ctx.current_ns;

  batch_clear(batch);

  while (!batch_full(batch)) {
    uint64_t t;
    struct flow *f;
    struct snbuf *pkt;

    t = this->events.top().first;
    f = this->events.top().second;
    if (!f || now < t) return;

    this->events.pop();

    if (f->packets_left <= 0) {
      cdlist_add_head(&this->flows_free, &f->free);
      this->active_flows--;
      continue;
    }

    pkt = this->FillPacket(f);

    if (f->first) {
      uint64_t delay_ns = this->NextFlowArrival();
      struct flow *new_f;

      new_f = this->ScheduleFlow(t + delay_ns);
      if (!new_f) {
        /* temporarily out of free flow data. retry. */
        this->events.push(std::pair<uint64_t, struct flow *>(t + RETRY_NS, f));
        continue;
      }

      f->first = 0;
    }

    f->packets_left--;

    this->events.push(std::pair<uint64_t, struct flow *>(
        t + (uint64_t)(1e9 / this->flow_pps), f));

    if (pkt) batch_add(batch, pkt);
  }
}

struct task_result FlowGen::RunTask(void *arg) {
  struct pkt_batch batch;
  struct task_result ret;

  const int pkt_overhead = 24;

  this->GeneratePackets(&batch);
  if (batch.cnt > 0) run_next_module(this, &batch);

  ret = (struct task_result){
      .packets = static_cast<uint64_t>(batch.cnt),
      .bits = static_cast<uint64_t>(
          ((this->template_size + pkt_overhead) * batch.cnt) * 8),
  };

  return ret;
}

struct snobj *FlowGen::GetDesc() {
  return snobj_str_fmt("%d flows", this->active_flows);
}

struct snobj *FlowGen::GetDump() {
  struct snobj *r = snobj_map();

  {
    struct snobj *t = snobj_map();

    snobj_map_set(t, "allocated_flows", snobj_int(this->allocated_flows));
    snobj_map_set(t, "active_flows", snobj_int(this->active_flows));
    snobj_map_set(t, "generated_flows", snobj_int(this->generated_flows));

    snobj_map_set(r, "stats", t);
  }

  {
    struct snobj *t = snobj_map();

    snobj_map_set(t, "total_pps", snobj_double(this->total_pps));
    snobj_map_set(t, "flow_rate", snobj_double(this->flow_rate));
    snobj_map_set(t, "flow_duration", snobj_double(this->flow_duration));

    snobj_map_set(r, "load", t);
  }

  {
    struct snobj *t = snobj_map();

    snobj_map_set(t, "concurrent_flows", snobj_double(this->concurrent_flows));
    snobj_map_set(t, "flow_pps", snobj_double(this->flow_pps));
    snobj_map_set(t, "flow_pkts", snobj_double(this->flow_pkts));
    snobj_map_set(t, "flow_gap_ns", snobj_double(this->flow_gap_ns));

    snobj_map_set(r, "derived", t);
  }

  {
    struct snobj *t = snobj_map();

    snobj_map_set(t, "quick_rampup", snobj_int(this->quick_rampup));
    snobj_map_set(t, "arrival",
                  snobj_str(this->arrival == ARRIVAL_UNIFORM ? "uniform"
                                                             : "exponential"));
    snobj_map_set(
        t, "duration",
        snobj_str(this->duration == DURATION_UNIFORM ? "uniform" : "pareto"));

    snobj_map_set(r, "behavior", t);
  }

  if (this->duration == DURATION_PARETO) {
    struct snobj *t = snobj_map();

    snobj_map_set(t, "alpha", snobj_double(this->pareto.alpha));
    snobj_map_set(t, "mean", snobj_double(this->pareto.mean));
    snobj_map_set(t, "max", snobj_int(this->MaxFlowPkts()));

    snobj_map_set(r, "pareto", t);
  }

  return r;
}

ModuleClassRegister<FlowGen> flowgen("FlowGen", "flowgen",
                                     "generates packets on a flow basis");
