#ifndef BESS_GATE_H_
#define BESS_GATE_H_

#include <string>
#include <vector>

#include "pktbatch.h"
#include "utils/common.h"

class Module;

namespace bess {

typedef uint16_t gate_idx_t;

#define TRACK_GATES 1
#define TCPDUMP_GATES 1

#define INVALID_GATE UINT16_MAX

/* A module may have up to MAX_GATES input/output gates (separately). */
#define MAX_GATES 8192
#define DROP_GATE MAX_GATES
static_assert(MAX_GATES < INVALID_GATE, "invalid macro value");
static_assert(DROP_GATE <= MAX_GATES, "invalid macro value");

class Gate;

// Gate hooks allow you to run arbitrary code on the packets flowing through a
// gate before they get delievered to the upstream module.
// TODO(melvin): GateHooks should be structured like Modules/Drivers, so bessctl
// can attach/detach them at runtime.
class GateHook {
 public:
  explicit GateHook(const std::string &name, uint16_t priority = 0,
                    Gate *gate = nullptr)
      : gate_(gate), name_(name), priority_(priority) {}

  virtual ~GateHook() {}

  const std::string &name() const { return name_; }

  void set_gate(Gate *gate) { gate_ = gate; }

  uint16_t priority() const { return priority_; }

  virtual void ProcessBatch(const bess::PacketBatch *) {}

 protected:
  Gate *gate_;

 private:
  const std::string &name_;

  const uint16_t priority_;

  DISALLOW_COPY_AND_ASSIGN(GateHook);
};

inline bool GateHookComp(const GateHook *lhs, const GateHook *rhs) {
  return (lhs->priority() < rhs->priority());
}

class Gate {
 public:
  Gate(Module *m, gate_idx_t idx, void *arg)
      : module_(m), gate_idx_(idx), arg_(arg), hooks_() {}

  ~Gate() { ClearHooks(); }

  Module *module() const { return module_; }

  gate_idx_t gate_idx() const { return gate_idx_; }

  void *arg() const { return arg_; }

  const std::vector<GateHook *> &hooks() const { return hooks_; }

  // Inserts hook in priority order and returns 0 on success.
  int AddHook(GateHook *hook);

  GateHook *FindHook(const std::string &name);

  void RemoveHook(const std::string &name);

  void ClearHooks();

 private:
  /* immutable values */
  Module *module_;      /* the module this gate belongs to */
  gate_idx_t gate_idx_; /* input/output gate index of itself */

  /* mutable values below */
  void *arg_;

  // TODO(melvin): Consider using a map here instead. It gets rid of the need to
  // scan to find modules for queries. Not sure how priority would work in a
  // map, though.
  std::vector<GateHook *> hooks_;

  DISALLOW_COPY_AND_ASSIGN(Gate);
};

class IGate;

class OGate : public Gate {
 public:
  OGate(Module *m, gate_idx_t idx, void *arg)
      : Gate(m, idx, arg), igate_(), igate_idx_() {}

  void set_igate(IGate *ig) { igate_ = ig; }
  IGate *igate() const { return igate_; }

  void set_igate_idx(gate_idx_t idx) { igate_idx_ = idx; }
  gate_idx_t igate_idx() const { return igate_idx_; }

 private:
  IGate *igate_;
  gate_idx_t igate_idx_; /* cache for igate->gate_idx */

  DISALLOW_COPY_AND_ASSIGN(OGate);
};

class IGate : public Gate {
 public:
  IGate(Module *m, gate_idx_t idx, void *arg)
      : Gate(m, idx, arg), ogates_upstream_() {}

  const std::vector<OGate *> &ogates_upstream() const {
    return ogates_upstream_;
  }

  void PushOgate(OGate *og) { ogates_upstream_.push_back(og); }

  void RemoveOgate(const OGate *og);

 private:
  std::vector<OGate *> ogates_upstream_;
};

}  // namespace bess

#endif  // BESS_GATE_H_
