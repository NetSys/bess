#include "../module.h"

class Source : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);

  virtual struct task_result RunTask(void *arg);

  struct snobj *command_set_pkt_size(struct snobj *arg);
  struct snobj *command_set_burst(struct snobj *arg);

  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 1;
  /*
          static const std::vector<struct Command> cmds = {
                  {"set_pkt_size", (CmdFunc)&Source::command_set_pkt_size, 1},
                  {"set_burst", 	 (CmdFunc)&Source::command_set_burst,
     1},
          };
  */

 private:
  int pkt_size_;
  int burst_;
};

struct snobj *Source::Init(struct snobj *arg) {
  struct snobj *t;
  struct snobj *err;

  task_id_t tid = register_task(this, NULL);
  if (tid == INVALID_TASK_ID) return snobj_err(ENOMEM, "Task creation failed");

  pkt_size_ = 60;
  burst_ = MAX_PKT_BURST;

  if (!arg) return NULL;

  if ((t = snobj_eval(arg, "pkt_size")) != NULL) {
    err = command_set_pkt_size(t);
    if (err) return err;
  }

  if ((t = snobj_eval(arg, "burst")) != NULL) {
    err = command_set_burst(t);
    if (err) return err;
  }

  return NULL;
}

struct task_result Source::RunTask(void *arg) {
  struct pkt_batch batch;
  struct task_result ret;

  const int pkt_overhead = 24;

  const int pkt_size = ACCESS_ONCE(pkt_size_);
  const int burst = ACCESS_ONCE(burst_);

  uint64_t total_bytes = pkt_size * burst;

  int cnt = snb_alloc_bulk(batch.pkts, burst, pkt_size);

  if (cnt > 0) {
    batch.cnt = cnt;
    run_next_module(this, &batch);
  }

  ret = (struct task_result){
      .packets = static_cast<uint64_t>(cnt),
      .bits = (total_bytes + cnt * pkt_overhead) * 8,
  };

  return ret;
}

struct snobj *Source::command_set_pkt_size(struct snobj *arg) {
  uint64_t val;

  if (snobj_type(arg) != TYPE_INT)
    return snobj_err(EINVAL, "pkt_size must be an integer");

  val = snobj_uint_get(arg);

  if (val == 0 || val > SNBUF_DATA)
    return snobj_err(EINVAL, "Invalid packet size");

  pkt_size_ = val;

  return NULL;
}

struct snobj *Source::command_set_burst(struct snobj *arg) {
  uint64_t val;

  if (snobj_type(arg) != TYPE_INT)
    return snobj_err(EINVAL, "burst must be an integer");

  val = snobj_uint_get(arg);

  if (val == 0 || val > MAX_PKT_BURST)
    return snobj_err(EINVAL, "burst size must be [1,%d]", MAX_PKT_BURST);

  burst_ = val;

  return NULL;
}

ModuleClassRegister<Source> source(
    "Source", "source", "infinitely generates packets with uninitialized data");

// ADD_MODULE(Source, "source", "infinitely generates packets with uninitialized
// data", cmds)
