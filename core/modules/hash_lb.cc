#include "hash_lb.h"

#include <rte_hash_crc.h>

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

const Commands<Module> HashLB::cmds = {
    {"set_mode", MODULE_FUNC &HashLB::CommandSetMode, 0},
    {"set_gates", MODULE_FUNC &HashLB::CommandSetGates, 0},
};

const PbCommands HashLB::pb_cmds = {
    {"set_mode", MODULE_CMD_FUNC(&HashLB::CommandSetModePb), 0},
    {"set_gates", MODULE_CMD_FUNC(&HashLB::CommandSetGatesPb), 0}};

struct snobj *HashLB::CommandSetMode(struct snobj *arg) {
  const char *mode = snobj_str_get(arg);

  if (!mode) {
    return snobj_err(EINVAL, "argument must be a string");
  }

  if (strcmp(mode, "l2") == 0) {
    mode_ = LB_L2;
  } else if (mode && strcmp(mode, "l3") == 0) {
    mode_ = LB_L3;
  } else if (mode && strcmp(mode, "l4") == 0) {
    mode_ = LB_L4;
  } else {
    return snobj_err(EINVAL, "available LB modes: l2, l3, l4");
  }

  return nullptr;
}

bess::pb::ModuleCommandResponse HashLB::CommandSetModePb(
    const bess::pb::HashLBCommandSetModeArg &arg) {
  bess::pb::ModuleCommandResponse response;
  switch (arg.mode()) {
    case bess::pb::HashLBCommandSetModeArg::L2:
      mode_ = LB_L2;
      break;
    case bess::pb::HashLBCommandSetModeArg::L3:
      mode_ = LB_L3;
      break;
    case bess::pb::HashLBCommandSetModeArg::L4:
      mode_ = LB_L4;
      break;
    default:
      set_cmd_response_error(
          &response, pb_error(EINVAL, "available LB modes: l2, l3, l4"));
      return response;
  }
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

struct snobj *HashLB::CommandSetGates(struct snobj *arg) {
  if (snobj_type(arg) == TYPE_INT) {
    int gates = snobj_int_get(arg);

    if (gates < 0 || gates > MAX_HLB_GATES || gates > MAX_GATES) {
      return snobj_err(EINVAL, "no more than %d gates",
                       std::min(MAX_HLB_GATES, MAX_GATES));
    }

    num_gates_ = gates;
    for (int i = 0; i < gates; i++) {
      gates_[i] = i;
    }

  } else if (snobj_type(arg) == TYPE_LIST) {
    struct snobj *gates = arg;

    if (gates->size > MAX_HLB_GATES) {
      return snobj_err(EINVAL, "no more than %d gates", MAX_HLB_GATES);
    }

    for (size_t i = 0; i < gates->size; i++) {
      struct snobj *elem = snobj_list_get(gates, i);

      if (snobj_type(elem) != TYPE_INT) {
        return snobj_err(EINVAL, "'gate' must be an integer");
      }

      gates_[i] = snobj_int_get(elem);
      if (!is_valid_gate(gates_[i])) {
        return snobj_err(EINVAL, "invalid gate %d", gates_[i]);
      }
    }

    num_gates_ = gates->size;
  } else {
    return snobj_err(EINVAL,
                     "argument must specify a gate "
                     "or a list of gates");
  }

  return nullptr;
}

bess::pb::ModuleCommandResponse HashLB::CommandSetGatesPb(
    const bess::pb::HashLBCommandSetGatesArg &arg) {
  bess::pb::ModuleCommandResponse response;

  if (arg.gates_size() > MAX_HLB_GATES) {
    set_cmd_response_error(
        &response, pb_error(EINVAL, "no more than %d gates", MAX_HLB_GATES));
    return response;
  }

  for (int i = 0; i < arg.gates_size(); i++) {
    gates_[i] = arg.gates(i);
    if (!is_valid_gate(gates_[i])) {
      set_cmd_response_error(&response,
                             pb_error(EINVAL, "invalid gate %d", gates_[i]));
      return response;
    }
  }

  num_gates_ = arg.gates_size();

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

struct snobj *HashLB::Init(struct snobj *arg) {
  struct snobj *t;

  mode_ = DEFAULT_MODE;

  if (!arg || snobj_type(arg) != TYPE_MAP) {
    return snobj_err(EINVAL, "empty argument");
  }

  if ((t = snobj_eval(arg, "gates"))) {
    struct snobj *err = CommandSetGates(t);
    if (err) {
      return err;
    }
  } else {
    return snobj_err(EINVAL, "'gates' must be specified");
  }

  if ((t = snobj_eval(arg, "mode"))) {
    return CommandSetMode(t);
  }

  return nullptr;
}

pb_error_t HashLB::InitPb(const bess::pb::HashLBArg &arg) {
  mode_ = DEFAULT_MODE;

  if (arg.has_gate_arg()) {
    bess::pb::ModuleCommandResponse response =
        CommandSetGatesPb(arg.gate_arg());
    pb_error_t err = response.error();
    if (err.err() != 0) {
      return err;
    }
  } else {
    return pb_error(EINVAL, "'gates' must be specified");
  }

  if (arg.has_mode_arg()) {
    bess::pb::ModuleCommandResponse response = CommandSetModePb(arg.mode_arg());
    return response.error();
  }

  return pb_errno(0);
}

void HashLB::LbL2(struct pkt_batch *batch, gate_idx_t *out_gates) {
  for (int i = 0; i < batch->cnt; i++) {
    struct snbuf *snb = batch->pkts[i];
    char *head = static_cast<char *>(snb_head_data(snb));

    uint64_t v0 = *(reinterpret_cast<uint64_t *>(head));
    uint32_t v1 = *(reinterpret_cast<uint32_t *>(head + 8));

    uint32_t hash_val = hash_64(v0, v1);

    out_gates[i] = gates_[hash_range(hash_val, num_gates_)];
  }
}

void HashLB::LbL3(struct pkt_batch *batch, gate_idx_t *out_gates) {
  /* assumes untagged packets */
  const int ip_offset = 14;

  for (int i = 0; i < batch->cnt; i++) {
    struct snbuf *snb = batch->pkts[i];
    char *head = static_cast<char *>(snb_head_data(snb));

    uint32_t hash_val;
    uint64_t v = *(reinterpret_cast<uint64_t *>(head + ip_offset + 12));

    hash_val = hash_64(v, 0);

    out_gates[i] = gates_[hash_range(hash_val, num_gates_)];
  }
}

void HashLB::LbL4(struct pkt_batch *batch, gate_idx_t *out_gates) {
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

    out_gates[i] = gates_[hash_range(hash_val, num_gates_)];
  }
}

void HashLB::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t out_gates[MAX_PKT_BURST];

  switch (mode_) {
    case LB_L2:
      LbL2(batch, out_gates);
      break;

    case LB_L3:
      LbL3(batch, out_gates);
      break;

    case LB_L4:
      LbL4(batch, out_gates);
      break;

    default:
      assert(0);
  }

  RunSplit(out_gates, batch);
}

ADD_MODULE(HashLB, "hash_lb",
           "splits packets on a flow basis with L2/L3/L4 header fields")
