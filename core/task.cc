// Copyright (c) 2014-2017, The Regents of the University of California.
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

#include "task.h"

#include <unordered_set>

#include "gate.h"
#include "module.h"

// Called when the leaf that owns this task is destroyed.
void Task::Detach() {
  c_ = nullptr;
}

// Called when the leaf that owns this task is created.
void Task::Attach(bess::LeafTrafficClass *c) {
  c_ = c;
}

void Task::AddToRun(bess::IGate *ig, bess::PacketBatch *batch) const {
  if (next_gate_ == nullptr && !ig->mergeable()) {  // chained
    next_gate_ = ig;
    next_batch_ = batch;
  } else {
    if (get_ibatch(ig)) {
      // FIXME check whether it will exceeds bounds
      get_ibatch(ig)->add(batch);
    } else {
      set_ibatch(ig, batch);
      igates_to_run_.push(std::make_pair(ig, batch));
    }
  }
}

struct task_result Task::operator()(void) const {
  bess::PacketBatch init_batch;
  ClearPacketBatch();

  // Start from the first module (task module)
  struct task_result result = module_->RunTask(this, &init_batch, arg_);

  // next_gate_: Continuously run if modules are chainned
  // igates_to_run_ : If next module connection is not chained (merged),
  // check priority to choose which module run next
  while (1) {
    bess::IGate *igate;
    bess::PacketBatch *batch;

    // choose igate and batch to run next
    if (next_gate_) {
      igate = next_gate_;
      batch = next_batch_;
      next_gate_ = nullptr;
      next_batch_ = nullptr;
    } else {
      if (igates_to_run_.empty()) {
        break;
      }

      auto item = igates_to_run_.top();
      igates_to_run_.pop();

      igate = item.first;
      batch = item.second;

      set_ibatch(igate, nullptr);
    }

    set_current_igate(igate->gate_idx());

    for (auto &hook : igate->hooks()) {
      hook->ProcessBatch(batch);
    }

    // process module
    igate->module()->ProcessBatch(this, batch);

    // process ogates
    igate->module()->ProcessOGates(this);

    bess::Packet::Free(&dead_batch_);
    dead_batch_.clear();
  }

  return result;
}

// Compute constraints for the pipeline starting at this task.
placement_constraint Task::GetSocketConstraints() const {
  if (module_) {
    std::unordered_set<const Module *> visited;
    return module_->ComputePlacementConstraints(&visited);
  } else {
    return UNCONSTRAINED_SOCKET;
  }
}

// Add a worker to the set of workers that call this task.
void Task::AddActiveWorker(int wid) const {
  if (module_) {
    module_->AddActiveWorker(wid, c_->task());
  }
}
