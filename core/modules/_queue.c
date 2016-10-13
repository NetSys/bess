#include "../kmod/llring.h"

#include "../module.h"

#define DEFAULT_QUEUE_SIZE 1024

class Queue : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void Deinit();

  virtual struct task_result RunTask(void *arg);
  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual struct snobj *GetDesc();

  struct snobj *RunCommand(const std::string &user_cmd, struct snobj *arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

 private:
  int Resize(int slots);
  struct snobj *CommandSetBurst(struct snobj *arg);
  struct snobj *CommandSetSize(struct snobj *arg);

  struct llring *queue;
  int prefetch;
  int burst;
};

int Queue::Resize(int slots) {
  struct llring *old_queue = this->queue;
  struct llring *new_queue;

  int bytes = llring_bytes_with_slots(slots);

  int ret;

  new_queue = static_cast<llring *>(mem_alloc(bytes));
  if (!new_queue) return -ENOMEM;

  ret = llring_init(new_queue, slots, 0, 1);
  if (ret) {
    mem_free(new_queue);
    return -EINVAL;
  }

  /* migrate packets from the old queue */
  if (old_queue) {
    struct snbuf *pkt;

    while (llring_sc_dequeue(old_queue, (void **)&pkt) == 0) {
      ret = llring_sp_enqueue(new_queue, pkt);
      if (ret == -LLRING_ERR_NOBUF) snb_free(pkt);
    }

    mem_free(old_queue);
  }

  this->queue = new_queue;

  return 0;
}

struct snobj *Queue::Init(struct snobj *arg) {
  task_id_t tid;

  struct snobj *t;
  struct snobj *err;

  this->burst = MAX_PKT_BURST;

  tid = register_task(this, NULL);
  if (tid == INVALID_TASK_ID) return snobj_err(ENOMEM, "Task creation failed");

  if ((t = snobj_eval(arg, "burst")) != NULL) {
    err = this->CommandSetBurst(t);
    if (err) return err;
  }

  if ((t = snobj_eval(arg, "size")) != NULL) {
    err = this->CommandSetSize(t);
    if (err) return err;
  } else {
    int ret = this->Resize(DEFAULT_QUEUE_SIZE);
    if (ret) return snobj_errno(-ret);
  }

  if (snobj_eval_int(arg, "prefetch")) this->prefetch = 1;

  return NULL;
}

void Queue::Deinit() {
  struct snbuf *pkt;

  while (llring_sc_dequeue(this->queue, (void **)&pkt) == 0) snb_free(pkt);

  mem_free(this->queue);
}

struct snobj *Queue::GetDesc() {
  const struct llring *ring = this->queue;

  return snobj_str_fmt("%u/%u", llring_count(ring), ring->common.slots);
}

/* from upstream */
void Queue::ProcessBatch(struct pkt_batch *batch) {
  int queued =
      llring_mp_enqueue_burst(this->queue, (void **)batch->pkts, batch->cnt);

  if (queued < batch->cnt)
    snb_free_bulk(batch->pkts + queued, batch->cnt - queued);
}

/* to downstream */
struct task_result Queue::RunTask(void *arg) {
  struct pkt_batch batch;
  struct task_result ret;

  const int burst = ACCESS_ONCE(this->burst);
  const int pkt_overhead = 24;

  uint64_t total_bytes = 0;

  uint64_t cnt =
      llring_sc_dequeue_burst(this->queue, (void **)batch.pkts, burst);

  if (cnt > 0) {
    batch.cnt = cnt;
    run_next_module(this, &batch);
  }

  if (this->prefetch) {
    for (uint64_t i = 0; i < cnt; i++) {
      total_bytes += snb_total_len(batch.pkts[i]);
      rte_prefetch0(snb_head_data(batch.pkts[i]));
    }
  } else {
    for (uint64_t i = 0; i < cnt; i++)
      total_bytes += snb_total_len(batch.pkts[i]);
  }

  ret = (struct task_result){
      .packets = cnt, .bits = (total_bytes + cnt * pkt_overhead) * 8,
  };

  return ret;
}

struct snobj *Queue::RunCommand(const std::string &user_cmd,
                                struct snobj *arg) {
  if (user_cmd == "set_burst") {
    return this->CommandSetBurst(arg);
  } else if (user_cmd == "set_size") {
    return this->CommandSetSize(arg);
  }
  return snobj_err(EINVAL, "invalid command");
}

struct snobj *Queue::CommandSetBurst(struct snobj *arg) {
  uint64_t val;

  if (snobj_type(arg) != TYPE_INT)
    return snobj_err(EINVAL, "burst must be an integer");

  val = snobj_uint_get(arg);

  if (val == 0 || val > MAX_PKT_BURST)
    return snobj_err(EINVAL, "burst size must be [1,%d]", MAX_PKT_BURST);

  this->burst = val;

  return NULL;
}

struct snobj *Queue::CommandSetSize(struct snobj *arg) {
  uint64_t val;
  int ret;

  if (snobj_type(arg) != TYPE_INT)
    return snobj_err(EINVAL, "argument must be an integer");

  val = snobj_uint_get(arg);

  if (val < 4 || val > 16384) return snobj_err(EINVAL, "must be in [4, 16384]");

  if (val & (val - 1)) return snobj_err(EINVAL, "must be a power of 2");

  ret = this->Resize(val);
  if (ret) return snobj_errno(-ret);

  return NULL;
}

ModuleClassRegister<Queue> queue(
    "Queue", "queue",
    "terminates current task and enqueue packets for new task");
