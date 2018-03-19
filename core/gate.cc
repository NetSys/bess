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

#include "gate.h"
#include "gate_hooks/track.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "worker.h"

namespace bess {

const GateHookCommands GateHook::cmds;

bool GateHookFactory::RegisterGateHook(GateHook::constructor_t constructor,
                                       const GateHookCommands &cmds,
                                       GateHook::init_func_t init_func,
                                       const std::string &hook_name) {
  return all_gate_hook_factories_holder()
      .emplace(std::piecewise_construct, std::forward_as_tuple(hook_name),
               std::forward_as_tuple(constructor, cmds, init_func, hook_name))
      .second;
}

std::map<std::string, GateHookFactory>
    &GateHookFactory::all_gate_hook_factories_holder(bool reset) {
  // Maps from hook names to hook factories. Tracks all hooks (via their
  // GateHookFactorys).
  static std::map<std::string, GateHookFactory> all_gate_hook_factories;

  if (reset) {
    all_gate_hook_factories.clear();
  }

  return all_gate_hook_factories;
}

const std::map<std::string, GateHookFactory>
    &GateHookFactory::all_gate_hook_factories() {
  return all_gate_hook_factories_holder();
}

// Creates new gate hook from the given factory, initializes it,
// and adds it to the given gate.  If any of these steps go wrong
// the gatehook instance is destroyed and we return a failed
// CommandResponse, otherwise we return a successful one.
CommandResponse Gate::NewGateHook(const GateHookFactory *factory, Gate *gate,
                                  bool is_igate,
                                  const google::protobuf::Any &arg) {
  bess::GateHook *hook = factory->hook_constructor_();
  CommandResponse init_ret = factory->hook_init_func_(hook, gate, arg);
  if (init_ret.error().code() != 0) {
    delete hook;
    return init_ret;
  }
  hook->set_factory(factory);
  hook->set_arg(arg);
  hook->set_gate(gate);
  int ret = gate->AddHook(hook);
  if (ret != 0) {
    delete hook;
    return CommandFailure(ret, "Unable to add hook '%s' to '%s' %cgate '%hu'",
                          factory->hook_name_.c_str(),
                          gate->module()->name().c_str(), is_igate ? 'i' : 'o',
                          gate->gate_idx());
  }
  return CommandSuccess();
}

int Gate::AddHook(GateHook *hook) {
  for (const auto &h : hooks_) {
    if (h->name() == hook->name()) {
      return EEXIST;
    }
  }

  hooks_.push_back(hook);

  const auto cmp = [](const GateHook *lhs, const GateHook *rhs) {
    return *lhs < *rhs;
  };
  std::sort(hooks_.begin(), hooks_.end(), cmp);

  return 0;
}

GateHook *Gate::FindHook(const std::string &name) {
  for (const auto &hook : hooks_) {
    if (hook->name() == name) {
      return hook;
    }
  }
  return nullptr;
}

void Gate::RemoveHook(const std::string &name) {
  for (auto it = hooks_.begin(); it != hooks_.end(); ++it) {
    GateHook *hook = *it;
    if (hook->name() == name) {
      delete hook;
      hooks_.erase(it);
      return;
    }
  }
}

// TODO(torek): combine (template) with ModuleBuilder::RunCommand
CommandResponse GateHookFactory::RunCommand(
    GateHook *hook, const std::string &user_cmd,
    const google::protobuf::Any &arg) const {
  Module *mod = hook->gate()->module();
  for (auto &cmd : cmds_) {
    if (user_cmd == cmd.cmd) {
      if (cmd.mt_safe != GateHookCommand::THREAD_SAFE &&
          mod->HasRunningWorker()) {
        return CommandFailure(EBUSY,
                              "There is a running worker and command "
                              "'%s' is not MT safe",
                              cmd.cmd.c_str());
      }

      return cmd.func(hook, arg);
    }
  }

  return CommandFailure(ENOTSUP, "'%s' does not support command '%s'",
                        hook_name_.c_str(), user_cmd.c_str());
}

void Gate::ClearHooks() {
  for (auto &hook : hooks_) {
    delete hook;
  }
  hooks_.clear();
}

void IGate::PushOgate(OGate *og) {
  ogates_upstream_.push_back(og);
  mergeable_ = (ogates_upstream_.size() > 1);
}

void IGate::RemoveOgate(const OGate *og) {
  for (auto it = ogates_upstream_.begin(); it != ogates_upstream_.end(); ++it) {
    if (*it == og) {
      ogates_upstream_.erase(it);
      return;
    }
  }
  mergeable_ = (ogates_upstream_.size() > 1);
}

// Add internally-generated Track() hook to this ogate.
void OGate::AddTrackHook() {
  static const GateHookFactory *track_factory;
  static google::protobuf::Any arg;

  // If we haven't located the track hook factory yet, do that first.
  if (track_factory == nullptr) {
    const auto it =
        bess::GateHookFactory::all_gate_hook_factories().find(Track::kName);
    // Would like to use CHECK_NE here, but cannot because
    // operator<< is not defined on the arguments.
    if (it == bess::GateHookFactory::all_gate_hook_factories().end()) {
      CHECK(0) << "track gate hook factory is missing";
    }
    track_factory = &it->second;

    // A new Track() instance takes an argument that has a "bits" flag.
    static bess::pb::TrackArg track_arg;
    track_arg.set_bits(false);
    arg.PackFrom(track_arg);
  }
  this->NewGateHook(track_factory, this, false, arg);
}

void OGate::SetIgate(IGate *ig) {
  igate_ = ig;
  igate_idx_ = ig->gate_idx();
}

}  // namespace bess
