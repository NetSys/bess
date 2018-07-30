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

#include "resume_hook.h"

#include <map>
#include <set>
#include <string>
#include <utility>

namespace bess {

std::set<std::unique_ptr<ResumeHook>, ResumeHook::UniquePtrLess>
    global_resume_hooks;

std::map<std::string, ResumeHookBuilder>
    &ResumeHookBuilder::all_resume_hook_builders_holder() {
  // Maps from hook names to hook builders. Tracks all hooks (via their
  // ResumeHookBuilders).
  static std::map<std::string, ResumeHookBuilder> all_resume_hook_builders;

  return all_resume_hook_builders;
}

const std::map<std::string, ResumeHookBuilder>
    &ResumeHookBuilder::all_resume_hook_builders() {
  return all_resume_hook_builders_holder();
}

bool ResumeHookBuilder::RegisterResumeHook(
    ResumeHook::constructor_t constructor, ResumeHook::init_func_t init_func,
    const std::string &hook_name) {
  return all_resume_hook_builders_holder()
      .emplace(std::piecewise_construct, std::forward_as_tuple(hook_name),
               std::forward_as_tuple(constructor, init_func, hook_name))
      .second;
}

void run_global_resume_hooks(bool run_modules) {
  auto &hooks = global_resume_hooks;

  for (auto &hook : hooks) {
    VLOG(1) << "Running global resume hook '" << hook->name() << "'";
    hook->Run();
  }

  if (run_modules) {
    auto &resume_modules = event_modules[Event::PreResume];
    for (auto it = resume_modules.begin(); it != resume_modules.end();) {
      int ret = (*it)->OnEvent(Event::PreResume);
      if (ret == -ENOTSUP) {
        it = resume_modules.erase(it);
      } else {
        it++;
      }
    }
  }
}

}  // namespace bess
