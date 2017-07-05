// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_GATE_H_
#define BESS_GATE_H_

#include <string>
#include <vector>

#include <grpc++/server.h>
#include <grpc/grpc.h>

#include "message.h"
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
class GateHook {
 public:
  explicit GateHook(const std::string &name, uint16_t priority = 0,
                    Gate *gate = nullptr)
      : gate_(gate), name_(name), priority_(priority) {}

  virtual ~GateHook() {}

  const std::string &name() const { return name_; }

  const Gate *gate() const { return gate_; }

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

using hook_constructor_t = std::function<GateHook *()>;

using hook_init_func_t = std::function<CommandResponse(
    GateHook *, const Gate *, const google::protobuf::Any &)>;

class GateHookFactory {
 public:
  GateHookFactory(hook_constructor_t constructor, hook_init_func_t init_func,
                  const std::string &hook_name)
      : hook_constructor_(constructor),
        hook_init_func_(init_func),
        hook_name_(hook_name) {}

  static bool RegisterGateHook(hook_constructor_t constructor,
                               hook_init_func_t init_func,
                               const std::string &hook_name);

  static std::map<std::string, GateHookFactory> &all_gate_hook_factories_holder(
      bool reset = false);

  static const std::map<std::string, GateHookFactory>
      &all_gate_hook_factories();

  GateHook *CreateGateHook() const { return hook_constructor_(); }

  CommandResponse InitGateHook(GateHook *h, const Gate *g,
                               const google::protobuf::Any &arg) const {
    return hook_init_func_(h, g, arg);
  }

 private:
  hook_constructor_t hook_constructor_;
  hook_init_func_t hook_init_func_;
  std::string hook_name_;
};

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

template <typename H, typename A>
static inline bess::hook_init_func_t InitHookWithGenericArg(
    CommandResponse (H::*fn)(const bess::Gate *, const A &)) {
  return [fn](bess::GateHook *h, const bess::Gate *g,
              const google::protobuf::Any &arg) {
    A arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<H *>(h), g, arg_);
  };
}

#define ADD_GATE_HOOK(_HOOK)                                           \
  bool __gate_hook__##_HOOK = bess::GateHookFactory::RegisterGateHook( \
      std::function<bess::GateHook *()>([]() { return new _HOOK(); }), \
      InitHookWithGenericArg(&_HOOK::Init), _HOOK::kName);

#endif  // BESS_GATE_H_
