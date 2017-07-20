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

#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace bess {

bool GateHookFactory::RegisterGateHook(hook_constructor_t constructor,
                                       hook_init_func_t init_func,
                                       const std::string &hook_name) {
  return all_gate_hook_factories_holder()
      .emplace(std::piecewise_construct, std::forward_as_tuple(hook_name),
               std::forward_as_tuple(constructor, init_func, hook_name))
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

int Gate::AddHook(GateHook *hook) {
  for (const auto &h : hooks_) {
    if (h->name() == hook->name()) {
      return EEXIST;
    }
  }

  hooks_.push_back(hook);
  std::sort(hooks_.begin(), hooks_.end(), GateHookComp);
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

void Gate::ClearHooks() {
  for (auto &hook : hooks_) {
    delete hook;
  }
  hooks_.clear();
}

void IGate::RemoveOgate(const OGate *og) {
  for (auto it = ogates_upstream_.begin(); it != ogates_upstream_.end(); ++it) {
    if (*it == og) {
      ogates_upstream_.erase(it);
      return;
    }
  }
}
}  // namespace bess
