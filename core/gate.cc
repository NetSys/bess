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
