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

#ifndef BESS_RESUME_HOOK_H_
#define BESS_RESUME_HOOK_H_

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "message.h"
#include "module.h"
#include "utils/format.h"

namespace bess {

// Resume hooks allow you to run arbitrary code before a worker is resumed by
// bessctl::ResumeWorker() or bessctl::ResumeAll(). Resume hooks may be attached
// to workers and modules, modules will have their resume hooks called exactly
// once per call to bessctl::ResumeAll() and bessctl::ResumeWorker() if an
// attached worker is being resumed.
class ResumeHook {
 public:
  using constructor_t = std::function<ResumeHook *()>;

  using init_func_t = std::function<CommandResponse(
      ResumeHook *, const google::protobuf::Any &)>;

  explicit ResumeHook(const std::string &name, uint16_t priority = 0,
                      bool is_default = false)
      : name_(name), priority_(priority), is_default_(is_default) {}

  virtual ~ResumeHook() {}

  const std::string &name() const { return name_; }

  uint16_t priority() const { return priority_; }

  bool is_default() const { return is_default_; }

  virtual void Run() = 0;

  bool operator<(const ResumeHook &rhs) const {
    return std::tie(priority_, name_) < std::tie(rhs.priority_, rhs.name_);
  }

  class UniquePtrLess {
   public:
    bool operator()(const std::unique_ptr<ResumeHook> &lhs,
                    const std::unique_ptr<ResumeHook> &rhs) const {
      return *lhs < *rhs;
    }
  };

 private:
  const std::string &name_;

  const uint16_t priority_;

  const bool is_default_;
};

class ResumeHookBuilder {
 public:
  ResumeHookBuilder(typename ResumeHook::constructor_t constructor,
                    typename ResumeHook::init_func_t init_func,
                    const std::string &hook_name)
      : hook_constructor_(constructor),
        hook_init_func_(init_func),
        hook_name_(hook_name) {}

  static bool RegisterResumeHook(typename ResumeHook::constructor_t constructor,
                                 typename ResumeHook::init_func_t init_func,
                                 const std::string &hook_name);

  const std::string &hook_name() const { return hook_name_; }

  static std::map<std::string, ResumeHookBuilder>
      &all_resume_hook_builders_holder();

  static const std::map<std::string, ResumeHookBuilder>
      &all_resume_hook_builders();

  std::unique_ptr<ResumeHook> CreateResumeHook() const {
    return std::unique_ptr<ResumeHook>(hook_constructor_());
  }

  CommandResponse InitResumeHook(ResumeHook *h,
                                 const google::protobuf::Any &arg) const {
    return hook_init_func_(h, arg);
  }

 private:
  typename ResumeHook::constructor_t hook_constructor_;
  typename ResumeHook::init_func_t hook_init_func_;
  std::string hook_name_;
};

extern std::set<std::unique_ptr<ResumeHook>, ResumeHook::UniquePtrLess>
    global_resume_hooks;

void run_global_resume_hooks(bool run_modules = true);

}  // namespace bess

template <typename H, typename A>
static inline typename bess::ResumeHook::init_func_t
InitResumeHookWithGenericArg(CommandResponse (H::*fn)(const A &)) {
  return [fn](bess::ResumeHook *h, const google::protobuf::Any &arg) {
    A arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<H *>(h), arg_);
  };
}

#define ADD_RESUME_HOOK(_HOOK)                                               \
  bool __resume_hook__##_HOOK = bess::ResumeHookBuilder::RegisterResumeHook( \
      std::function<bess::ResumeHook *()>([]() { return new _HOOK(); }),     \
      InitResumeHookWithGenericArg(&_HOOK::Init), _HOOK::kName);

#endif  // BESS_RESUME_HOOK_H_
