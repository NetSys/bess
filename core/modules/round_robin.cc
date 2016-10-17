#include "../module.h"

#define MAX_RR_GATES 16384

static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

class RoundRobin : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const std::vector<struct Command> cmds;

 private:
  struct snobj *CommandSetMode(struct snobj *arg);
  struct snobj *CommandSetGates(struct snobj *arg);

  /* XXX: currently doesn't support multiple workers */
  gate_idx_t gates_[MAX_RR_GATES] = {};
  int ngates_ = {};
  int current_gate_ = {};
  int per_packet_ = {};
};

const std::vector<struct Command> RoundRobin::cmds = {
    {"set_mode", static_cast<CmdFunc>(&RoundRobin::CommandSetMode), 0},
    {"set_gates", static_cast<CmdFunc>(&RoundRobin::CommandSetGates), 0},
};

struct snobj *RoundRobin::Init(struct snobj *arg) {
  struct snobj *t;

  if (!arg || snobj_type(arg) != TYPE_MAP)
    return snobj_err(EINVAL, "empty argument");

  if ((t = snobj_eval(arg, "gates"))) {
    struct snobj *err = this->CommandSetGates(t);
    if (err) return err;
  } else
    return snobj_err(EINVAL, "'gates' must be specified");

  if ((t = snobj_eval(arg, "mode"))) return this->CommandSetMode(t);

  return NULL;
}

struct snobj *RoundRobin::CommandSetMode(struct snobj *arg) {
  const char *mode = snobj_str_get(arg);

  if (!mode) return snobj_err(EINVAL, "argument must be a string");

  if (strcmp(mode, "packet") == 0)
    this->per_packet_ = 1;
  else if (strcmp(mode, "batch") == 0)
    this->per_packet_ = 0;
  else
    return snobj_err(EINVAL, "argument must be either 'packet' or 'batch'");

  return NULL;
}

struct snobj *RoundRobin::CommandSetGates(struct snobj *arg) {
  if (snobj_type(arg) == TYPE_INT) {
    int gates = snobj_int_get(arg);

    if (gates < 0 || gates > MAX_RR_GATES || gates > MAX_GATES)
      return snobj_err(EINVAL, "no more than %d gates",
                       std::min(MAX_RR_GATES, MAX_GATES));

    this->ngates_ = gates;
    for (int i = 0; i < gates; i++) this->gates_[i] = i;

  } else if (snobj_type(arg) == TYPE_LIST) {
    struct snobj *gates = arg;

    if (gates->size > MAX_RR_GATES)
      return snobj_err(EINVAL, "no more than %d gates", MAX_RR_GATES);

    for (size_t i = 0; i < gates->size; i++) {
      struct snobj *elem = snobj_list_get(gates, i);

      if (snobj_type(elem) != TYPE_INT)
        return snobj_err(EINVAL, "'gate' must be an integer");

      this->gates_[i] = snobj_int_get(elem);
      if (!is_valid_gate(this->gates_[i]))
        return snobj_err(EINVAL, "invalid gate %d", this->gates_[i]);
    }

    this->ngates_ = gates->size;

  } else
    return snobj_err(EINVAL,
                     "argument must specify a gate "
                     "or a list of gates");

  return NULL;
}

void RoundRobin::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t ogates[MAX_PKT_BURST];

  if (this->per_packet_) {
    for (int i = 0; i < batch->cnt; i++) {
      ogates[i] = this->gates_[this->current_gate_];
      this->current_gate_ = (this->current_gate_ + 1) % this->ngates_;
    }
    run_split(this, ogates, batch);
  } else {
    gate_idx_t gate = this->gates_[this->current_gate_];
    this->current_gate_ = (this->current_gate_ + 1) % this->ngates_;
    run_choose_module(this, gate, batch);
  }
}

ADD_MODULE(RoundRobin, "rr", "splits packets evenly with round robin")
