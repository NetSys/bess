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

#ifndef BESS_TASK_H_
#define BESS_TASK_H_

#include <queue>
#include <string>

#include "gate.h"
#include "pktbatch.h"
#include "utils/extended_priority_queue.h"

struct task_result {
  bool block;
  uint32_t packets;
  uint64_t bits;
};

typedef uint16_t gate_idx_t;
typedef uint16_t task_id_t;
typedef uint64_t placement_constraint;

#define MAX_PBATCH_CNT 256

class Module;
struct Context;

namespace bess {
class LeafTrafficClass;
class PacketBatch;
}  // namespace bess

// Functor used by a leaf in a Worker's Scheduler to run a task in a module.
class Task {
 private:
  // Used by operator().
  Module *module_;
  void *arg_;                  // Auxiliary value passed to Module::RunTask().
  bess::LeafTrafficClass *c_;  // Leaf TC associated with this task.

  struct GateBatchGreater {
    bool operator()(
        const std::pair<bess::IGate *, bess::PacketBatch *> &left,
        const std::pair<bess::IGate *, bess::PacketBatch *> &right) const {
      return left.first->priority() > right.first->priority();
    }
  };

  // XXX Tasks needs to be non-const in workers/modules
  mutable bess::utils::extended_priority_queue<
      std::pair<bess::IGate *, bess::PacketBatch *>, GateBatchGreater>
      igates_to_run_;  // A queue for IGates to run

  mutable bess::IGate *next_gate_;  // Cache next module to run without merging
  // Optimization for chain
  mutable bess::PacketBatch
      *next_batch_;  // Cache to run next batch with next module

  mutable bess::PacketBatch
      dead_batch_;  // A packet batch for storing packets to free

  // Simple packet batch pool
  mutable int pbatch_idx_;
  mutable bess::PacketBatch *pbatch_;

  mutable std::vector<bess::PacketBatch *> gate_batch_;

 public:
  // When this task is scheduled it will execute 'm' with 'arg'.  When the
  // associated leaf is created/destroyed, 'module_task' will be updated.
  Task(Module *m, void *arg)
      : module_(m),
        arg_(arg),
        c_(),
        igates_to_run_(),
        next_gate_(),
        next_batch_(),
        pbatch_idx_(),
        pbatch_(
            new bess::PacketBatch[MAX_PBATCH_CNT]),  // XXX Need to adjust size
        gate_batch_(std::vector<bess::PacketBatch *>(64, 0)) {
    dead_batch_.clear();
  }

  ~Task() { delete[] pbatch_; }

  // Called when the leaf that owns this task is destroyed.
  void Detach();

  // Called when the leaf that owns this task is created.
  void Attach(bess::LeafTrafficClass *c);

  void AddToRun(bess::IGate *ig, bess::PacketBatch *batch) const {
    if (next_gate_ == nullptr &&
        !ig->mergeable()) {  // optimization for chained
      next_gate_ = ig;
      next_batch_ = batch;
    } else {
      bess::PacketBatch *ibatch = get_gate_batch(ig);
      if (ibatch && (static_cast<size_t>(ibatch->cnt() + batch->cnt()) <=
                     bess::PacketBatch::kMaxBurst)) {
        // merge two batches
        ibatch->add(batch);
      } else {
        // set the input as new batch
        set_gate_batch(ig, batch);
        igates_to_run_.emplace(ig, batch);
      }
    }
  }

  // Do not track used/unsued for efficiency
  bess::PacketBatch *AllocPacketBatch() const {
    DCHECK_LT(pbatch_idx_, MAX_PBATCH_CNT);
    bess::PacketBatch *batch = &pbatch_[pbatch_idx_++];
    batch->clear();
    return batch;
  }

  void UpdatePerGateBatch(uint32_t gate_cnt) const {
    if (gate_batch_.size() < gate_cnt) {
      gate_batch_.resize(gate_cnt, nullptr);
    }
  }

  void ClearPacketBatch() const { pbatch_idx_ = 0; }

  Module *module() const { return module_; }

  bess::PacketBatch *dead_batch() const { return &dead_batch_; }

  bess::PacketBatch *get_gate_batch(bess::Gate *gate) const {
    return gate_batch_[gate->global_gate_index()];
  }

  void set_gate_batch(bess::Gate *gate, bess::PacketBatch *batch) const {
    gate_batch_[gate->global_gate_index()] = batch;
  }

  bess::LeafTrafficClass *GetTC() const { return c_; }

  struct task_result operator()(Context *ctx) const;

  // Compute constraints for the pipeline starting at this task.
  placement_constraint GetSocketConstraints() const;

  // Add a worker to the set of workers that call this task.
  void AddActiveWorker(int wid) const;
};

#endif  // BESS_TASK_H_
