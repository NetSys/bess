#include "../module.h"
#include "../utils/random.h"

#include <rte_hash_crc.h>

#define MAX_HLB_GATES 16384

/* TODO: add symmetric mode (e.g., LB_L4_SYM), v6 mode, etc. */
enum LbMode {
  LB_L2, /* dst MAC + src MAC */
  LB_L3, /* src IP + dst IP */
  LB_L4  /* L4 proto + src IP + dst IP + src port + dst port */
};

const enum LbMode DEFAULT_MODE = LB_L4;

static inline uint32_t hash_64(uint64_t val, uint32_t init_val) {
#if __SSE4_2__ && __x86_64
  return crc32c_sse42_u64(val, init_val);
#else
  return crc32c_2words(val, init_val);
#endif
}

/* Returns a value in [0, range) as a function of an opaque number.
 * Also see utils/random.h */
static inline uint16_t hash_range(uint32_t hashval, uint16_t range) {
#if 1
  union {
    uint64_t i;
    double d;
  } tmp;

  /* the resulting number is 1.(b0)(b1)..(b31)00000..00 */
  tmp.i = 0x3ff0000000000000ul | ((uint64_t)hashval << 20);

  return (tmp.d - 1.0) * range;
#else
  /* This IDIV instruction is significantly slower */
  return hashval % range;
#endif
}

static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

class HashLB : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const std::vector<struct Command> cmds;

 private:
  void LbL2(struct pkt_batch *batch, gate_idx_t *ogates);
  void LbL3(struct pkt_batch *batch, gate_idx_t *ogates);
  void LbL4(struct pkt_batch *batch, gate_idx_t *ogates);

  struct snobj *CommandSetMode(struct snobj *arg);
  struct snobj *CommandSetGates(struct snobj *arg);

  gate_idx_t gates[MAX_HLB_GATES];
  int num_gates;
  enum LbMode mode;
};

const std::vector<struct Command> HashLB::cmds = {
    {"set_mode", static_cast<CmdFunc>(&HashLB::CommandSetMode), 0},
    {"set_gates", static_cast<CmdFunc>(&HashLB::CommandSetGates), 0},
};

struct snobj *HashLB::CommandSetMode(struct snobj *arg) {
  const char *mode = snobj_str_get(arg);

  if (!mode) return snobj_err(EINVAL, "argument must be a string");

  if (strcmp(mode, "l2") == 0)
    this->mode = LB_L2;
  else if (mode && strcmp(mode, "l3") == 0)
    this->mode = LB_L3;
  else if (mode && strcmp(mode, "l4") == 0)
    this->mode = LB_L4;
  else
    return snobj_err(EINVAL, "available LB modes: l2, l3, l4");

  return NULL;
}

struct snobj *HashLB::CommandSetGates(struct snobj *arg) {
  if (snobj_type(arg) == TYPE_INT) {
    int gates = snobj_int_get(arg);

    if (gates < 0 || gates > MAX_HLB_GATES || gates > MAX_GATES)
      return snobj_err(EINVAL, "no more than %d gates",
                       MIN(MAX_HLB_GATES, MAX_GATES));

    this->num_gates = gates;
    for (int i = 0; i < gates; i++) this->gates[i] = i;

  } else if (snobj_type(arg) == TYPE_LIST) {
    struct snobj *gates = arg;

    if (gates->size > MAX_HLB_GATES)
      return snobj_err(EINVAL, "no more than %d gates", MAX_HLB_GATES);

    for (size_t i = 0; i < gates->size; i++) {
      struct snobj *elem = snobj_list_get(gates, i);

      if (snobj_type(elem) != TYPE_INT)
        return snobj_err(EINVAL, "'gate' must be an integer");

      this->gates[i] = snobj_int_get(elem);
      if (!is_valid_gate(this->gates[i]))
        return snobj_err(EINVAL, "invalid gate %d", this->gates[i]);
    }

    this->num_gates = gates->size;

  } else
    return snobj_err(EINVAL,
                     "argument must specify a gate "
                     "or a list of gates");

  return NULL;
}

struct snobj *HashLB::Init(struct snobj *arg) {
  struct snobj *t;

  this->mode = DEFAULT_MODE;

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

void HashLB::LbL2(struct pkt_batch *batch, gate_idx_t *ogates) {
  for (int i = 0; i < batch->cnt; i++) {
    struct snbuf *snb = batch->pkts[i];
    char *head = static_cast<char *>(snb_head_data(snb));

    uint64_t v0 = *(reinterpret_cast<uint64_t *>(head));
    uint32_t v1 = *(reinterpret_cast<uint32_t *>(head + 8));

    uint32_t hash_val = hash_64(v0, v1);

    ogates[i] = this->gates[hash_range(hash_val, this->num_gates)];
  }
}

void HashLB::LbL3(struct pkt_batch *batch, gate_idx_t *ogates) {
  /* assumes untagged packets */
  const int ip_offset = 14;

  for (int i = 0; i < batch->cnt; i++) {
    struct snbuf *snb = batch->pkts[i];
    char *head = static_cast<char *>(snb_head_data(snb));

    uint32_t hash_val;
    uint64_t v = *(reinterpret_cast<uint64_t *>(head + ip_offset + 12));

    hash_val = hash_64(v, 0);

    ogates[i] = this->gates[hash_range(hash_val, this->num_gates)];
  }
}

void HashLB::LbL4(struct pkt_batch *batch, gate_idx_t *ogates) {
  /* assumes untagged packets without IP options */
  const int ip_offset = 14;
  const int l4_offset = ip_offset + 20;

  for (int i = 0; i < batch->cnt; i++) {
    struct snbuf *snb = batch->pkts[i];
    char *head = static_cast<char *>(snb_head_data(snb));

    uint32_t hash_val;
    uint64_t v0 = *(reinterpret_cast<uint64_t *>(head + ip_offset + 12));
    uint32_t v1 = *(reinterpret_cast<uint64_t *>(head + l4_offset)); /* ports */

    v1 ^= *(reinterpret_cast<uint32_t *>(head + ip_offset + 9)); /* ip_proto */

    hash_val = hash_64(v0, v1);

    ogates[i] = this->gates[hash_range(hash_val, this->num_gates)];
  }
}

void HashLB::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t ogates[MAX_PKT_BURST];

  switch (this->mode) {
    case LB_L2:
      this->LbL2(batch, ogates);
      break;

    case LB_L3:
      this->LbL3(batch, ogates);
      break;

    case LB_L4:
      this->LbL4(batch, ogates);
      break;

    default:
      assert(0);
  }

  run_split(this, ogates, batch);
}

ModuleClassRegister<HashLB> hash_lbl(
    "HashLB", "hash_lb",
    "splits packets on a flow basis with L2/L3/L4 header fields");
