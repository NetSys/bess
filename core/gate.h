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

#include "commands.h"
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
class GateHookBuilder;

// Gate hooks allow you to run arbitrary code on the packets flowing through a
// gate before they get delievered to the upstream module.
class GateHook {
 public:
  using constructor_t = std::function<GateHook *()>;

  using init_func_t = std::function<CommandResponse(
      GateHook *, const Gate *, const google::protobuf::Any &)>;

  explicit GateHook(const std::string &class_name, const std::string &name,
                    uint16_t priority = 0, Gate *gate = nullptr)
      : gate_(gate),
        class_name_(class_name),
        name_(name),
        priority_(priority),
        builder_() {}

  virtual ~GateHook() {}

  const std::string &class_name() const { return class_name_; }

  const std::string &name() const { return name_; }

  const Gate *gate() const { return gate_; }

  const google::protobuf::Any &arg() const { return arg_; }

  void set_class_name(const std::string &name) { class_name_ = name; }

  void set_name(const std::string &name) { name_ = name; }

  void set_gate(Gate *gate) { gate_ = gate; }

  void set_arg(const google::protobuf::Any &arg) { arg_ = arg; }

  uint16_t priority() const { return priority_; }

  virtual void ProcessBatch(const bess::PacketBatch *) {}

  CommandResponse RunCommand(const std::string &cmd,
                             const google::protobuf::Any &arg);

  void set_builder(const GateHookBuilder *builder) { builder_ = builder; }

  bool operator<(const GateHook &rhs) const {
    return std::tie(priority_, name_) < std::tie(rhs.priority_, rhs.name_);
  }

  static const GateHookCommands cmds;

 protected:
  Gate *gate_;

 private:
  std::string class_name_;
  std::string name_;
  const uint16_t priority_;
  const GateHookBuilder *builder_;
  google::protobuf::Any arg_;

  DISALLOW_COPY_AND_ASSIGN(GateHook);
};

struct CompareGatehookName : public std::unary_function<GateHook, GateHook> {
  explicit CompareGatehookName(const std::string &name) : name_(name) {}
  bool operator()(const GateHook *gatehook) {
    return name_ == gatehook->name();
  }
  std::string name_;
};

// A class for creating new 'gate hook's
class GateHookBuilder {
 public:
  GateHookBuilder(GateHook::constructor_t constructor,
                  const std::string &class_name,
                  const std::string &name_template,
                  const std::string &help_text, const GateHookCommands &cmds,
                  GateHook::init_func_t init_func)
      : hook_constructor_(constructor),
        class_name_(class_name),
        name_template_(name_template),
        help_text_(help_text),
        cmds_(cmds),
        hook_init_func_(init_func) {}

  static bool RegisterGateHook(GateHook::constructor_t constructor,
                               const std::string &class_name,
                               const std::string &name_template,
                               const std::string &help_text,
                               const GateHookCommands &cmds,
                               GateHook::init_func_t init_func);

  static std::map<std::string, GateHookBuilder> &all_gate_hook_builders_holder(
      bool reset = false);

  static const std::map<std::string, GateHookBuilder> &all_gate_hook_builders();

  const std::string &class_name() const { return class_name_; }
  const std::string &name_template() const { return name_template_; }
  const std::string &help_text() const { return help_text_; }

  const std::vector<std::pair<std::string, std::string>> cmds() const {
    std::vector<std::pair<std::string, std::string>> ret;
    for (auto &cmd : cmds_)
      ret.push_back(std::make_pair(cmd.cmd, cmd.arg_type));
    return ret;
  }

  CommandResponse RunCommand(GateHook *hook, const std::string &user_cmd,
                             const google::protobuf::Any &arg) const;

 private:
  friend class Gate;

  GateHook::constructor_t hook_constructor_;
  std::string class_name_;
  std::string name_template_;
  std::string help_text_;
  const GateHookCommands cmds_;
  GateHook::init_func_t hook_init_func_;
};

inline CommandResponse GateHook::RunCommand(const std::string &cmd,
                                            const google::protobuf::Any &arg) {
  return builder_->RunCommand(this, cmd, arg);
}

// A class for gate, will be inherited for input gates and output gates
class Gate {
 public:
  Gate(Module *m, gate_idx_t idx)
      : module_(m), gate_idx_(idx), global_gate_index_(), hooks_() {}

  virtual ~Gate() { ClearHooks(); }

  Module *module() const { return module_; }

  gate_idx_t gate_idx() const { return gate_idx_; }

  uint32_t global_gate_index() const { return global_gate_index_; }
  void SetUniqueIdx(uint32_t global_gate_index) {
    global_gate_index_ = global_gate_index;
  }

  const std::vector<GateHook *> &hooks() const { return hooks_; }

  // Creates, initializes, and then inserts gate hook in priority order.
  GateHook *CreateGateHook(const GateHookBuilder *builder, Gate *gate,
                           const std::string &name,
                           const google::protobuf::Any &arg, pb_error_t *error);

  GateHook *FindHook(const std::string &name);

  void RemoveHook(const std::string &name);

  GateHook *FindHookByClass(const std::string &name);

  void RemoveHookByClass(const std::string &name);

  void ClearHooks();

 protected:
  friend class GateTest;

  // Inserts hook in priority order and returns 0 on success.
  int AddHook(GateHook *hook);

  Module *module_;              // the module this gate belongs to
  gate_idx_t gate_idx_;         // input/output gate index of itself
  uint32_t global_gate_index_;  // a globally unique igate index

  std::vector<GateHook *> hooks_;

 private:
  const std::string GenerateDefaultName(const GateHookBuilder *builder,
                                        Gate *gate);

  DISALLOW_COPY_AND_ASSIGN(Gate);
};

class OGate;

// A class for input gate
class IGate : public Gate {
 public:
  IGate(Module *m, gate_idx_t idx)
      : Gate(m, idx), ogates_upstream_(), priority_(), mergeable_(false) {}

  const std::vector<OGate *> &ogates_upstream() const {
    return ogates_upstream_;
  }

  void SetPriority(uint32_t priority) { priority_ = priority; }

  uint32_t priority() const { return priority_; }
  bool mergeable() const { return mergeable_; }

  void PushOgate(OGate *og);
  void RemoveOgate(const OGate *og);

 private:
  std::vector<OGate *> ogates_upstream_;  // previous ogates connected with
  uint32_t priority_;  // priority to be scheduled with a task. lower number
                       // meaning higher priority.
  bool mergeable_;  // set to be true, if it is connected with multiple ogates
                    // so that the inputs can be merged and processed once

  DISALLOW_COPY_AND_ASSIGN(IGate);
};

// A class for output gate. It connects to an input gate of the next module.
class OGate : public Gate {
 public:
  OGate(Module *m, gate_idx_t idx, Module *next)
      : Gate(m, idx), next_(next), igate_(), igate_idx_() {}

  void SetIgate(IGate *ig);

  Module *next() const { return next_; }
  IGate *igate() const { return igate_; }
  gate_idx_t igate_idx() const { return igate_idx_; }

  void AddTrackHook();

 private:
  Module *next_;          // next module connected with
  IGate *igate_;          // next igate connected with
  gate_idx_t igate_idx_;  // cache for igate->gate_idx

  DISALLOW_COPY_AND_ASSIGN(OGate);
};

}  // namespace bess

template <typename T, typename H>
static inline gate_hook_cmd_func_t GATE_HOOK_CMD_FUNC(
    CommandResponse (H::*fn)(const T &)) {
  return [fn](bess::GateHook *h, const google::protobuf::Any &arg) {
    T arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<H *>(h), arg_);
  };
}

template <typename H, typename A>
static inline bess::GateHook::init_func_t InitGateHookWithGenericArg(
    CommandResponse (H::*fn)(const bess::Gate *, const A &)) {
  return [fn](bess::GateHook *h, const bess::Gate *g,
              const google::protobuf::Any &arg) {
    A arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<H *>(h), g, arg_);
  };
}

#define ADD_GATE_HOOK(_HOOK, _NAME_TEMPLATE, _HELP)                    \
  bool __gate_hook__##_HOOK = bess::GateHookBuilder::RegisterGateHook( \
      std::function<bess::GateHook *()>([]() { return new _HOOK(); }), \
      _HOOK::kName, _NAME_TEMPLATE, _HELP, _HOOK::cmds,                \
      InitGateHookWithGenericArg(&_HOOK::Init));

#endif  // BESS_GATE_H_
