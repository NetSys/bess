#ifndef BESS_GATE_H_
#define BESS_GATE_H_

#include <vector>

#include "utils/cdlist.h"

class Module;

typedef uint16_t gate_idx_t;

#define TRACK_GATES 1
#define TCPDUMP_GATES 1

#define INVALID_GATE UINT16_MAX

/* A module may have up to MAX_GATES input/output gates (separately). */
#define MAX_GATES 8192
#define DROP_GATE MAX_GATES
static_assert(MAX_GATES < INVALID_GATE, "invalid macro value");
static_assert(DROP_GATE <= MAX_GATES, "invalid macro value");

struct gate;

// Gate hooks allow you to run arbitrary code on the packets flowing through a
// gate before they get delievered to the upstream module.
// TODO(melvin): GateHooks should be structured like Modules/Drivers, so bessctl
// can attach/detach them at runtime.
class GateHook {
 public:
  GateHook(const std::string &name, uint16_t priority = 0,
           struct gate *gate = nullptr)
      : name_(name), priority_(priority), gate_(gate){};

  virtual ~GateHook(){};

  const std::string &name() const { return name_; }

  void set_gate(struct gate *gate) { gate_ = gate; }

  uint16_t priority() const { return priority_; }

  /// The main
  virtual void ProcessBatch(struct pkt_batch *){};

 private:
  DISALLOW_COPY_AND_ASSIGN(GateHook);

  const std::string &name_;

  const uint16_t priority_;

 protected:
  struct gate *gate_;
};

const std::vector<GateHook *> kNoHooks = {};

struct gate {
  /* immutable values */
  Module *m;           /* the module this gate belongs to */
  gate_idx_t gate_idx; /* input/output gate index of itself */

  /* mutable values below */
  void *arg;

  union {
    struct {
      struct cdlist_item igate_upstream;
      struct gate *igate;
      gate_idx_t igate_idx; /* cache for igate->gate_idx */
    } out;

    struct {
      struct cdlist_head ogates_upstream;
    } in;
  };

  std::vector<GateHook *> hooks;
};

// TODO(melvin): not sure this necessary anymore. consider replacing with
// std::vector
struct gates {
  /* Resizable array of 'struct gate *'.
   * Unconnected elements are filled with nullptr */
  struct gate **arr;

  /* The current size of the array.
   * Always <= m->mclass->num_[i|o]gates */
  gate_idx_t curr_size;
};

#endif
