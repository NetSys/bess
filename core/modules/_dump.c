#include <rte_hexdump.h>

#include "../module.h"

#define NS_PER_SEC 1000000000ul

static const uint64_t DEFAULT_INTERVAL_NS = 1 * NS_PER_SEC; /* 1 sec */

class Dump : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *RunCommand(const std::string &user_cmd, struct snobj *arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

 private:
  struct snobj *CommandSetInterval(struct snobj *arg);

  uint64_t min_interval_ns;
  uint64_t next_ns;
};

struct snobj *Dump::Init(struct snobj *arg) {
  this->min_interval_ns = DEFAULT_INTERVAL_NS;
  this->next_ns = ctx.current_tsc;

  if (arg && (arg = snobj_eval(arg, "interval")))
    return this->CommandSetInterval(arg);
  else
    return NULL;
}

void Dump::ProcessBatch(struct pkt_batch *batch) {
  if (unlikely(ctx.current_ns >= this->next_ns)) {
    struct snbuf *pkt = batch->pkts[0];

    printf("----------------------------------------\n");
    printf("%s: packet dump\n", this->Name().c_str());
    snb_dump(stdout, pkt);
    rte_hexdump(stdout, "Metadata buffer", pkt->_metadata, SNBUF_METADATA);
    this->next_ns = ctx.current_ns + this->min_interval_ns;
  }

  run_choose_module(this, get_igate(), batch);
}

struct snobj *Dump::RunCommand(const std::string &user_cmd, struct snobj *arg) {
  if (user_cmd == "set_interval") {
    return this->CommandSetInterval(arg);
  }
  assert(0);
}

struct snobj *Dump::CommandSetInterval(struct snobj *arg) {
  double sec = snobj_number_get(arg);

  if (isnan(sec) || sec < 0.0) return snobj_err(EINVAL, "invalid interval");

  this->min_interval_ns = static_cast<uint64_t>(sec * NS_PER_SEC);

  return NULL;
}

ModuleClassRegister<Dump> dump("Dump", "dump",
                               "Dump packet data and metadata attributes");
