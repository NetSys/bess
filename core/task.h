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
    bool operator()(std::pair<bess::IGate *, bess::PacketBatch *> left,
                    std::pair<bess::IGate *, bess::PacketBatch *> right) const {
      return left.first->priority() > right.first->priority();
    }
  };

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

  mutable std::vector<bess::PacketBatch *> ogate_batch_;
  mutable std::vector<bess::PacketBatch *> igate_batch_;

  /* The current input gate index is not given as a function parameter.
   * Modules should use get_igate() for access */
  mutable gate_idx_t current_igate_;

 public:
  // When this task is scheduled it will execute 'm' with 'arg'.  When the
  // associated leaf is created/destroyed, 'module_task' will be updated.
  Task(Module *m, void *arg)
      : module_(m),
        arg_(arg),
        c_(nullptr),
        igates_to_run_(),
        next_gate_(),
        next_batch_(),
        pbatch_idx_() {
    dead_batch_.clear();
    // XXX Need to adjust size
    pbatch_ = new bess::PacketBatch[MAX_PBATCH_CNT];

    ogate_batch_ = std::vector<bess::PacketBatch *>(64, 0);
    igate_batch_ = std::vector<bess::PacketBatch *>(64, 0);
  }

  ~Task() { delete[] pbatch_; }

  // Called when the leaf that owns this task is destroyed.
  void Detach();

  // Called when the leaf that owns this task is created.
  void Attach(bess::LeafTrafficClass *c);

  void AddToRun(bess::IGate *ig, bess::PacketBatch *batch) const;

  // Do not track used/unsued for efficiency
  bess::PacketBatch *AllocPacketBatch() const {
    CHECK_LT(pbatch_idx_, MAX_PBATCH_CNT);
    bess::PacketBatch *batch = &pbatch_[pbatch_idx_++];
    batch->clear();
    return batch;
  }

  void SetGateCnt(uint32_t igate_cnt, uint32_t ogate_cnt) const {
    if (igate_batch_.capacity() < igate_cnt) {
      igate_batch_.resize(igate_cnt, 0);
    }

    if (ogate_batch_.capacity() < ogate_cnt) {
      ogate_batch_.resize(ogate_cnt, 0);
    }
  }

  void ClearPacketBatch() const { pbatch_idx_ = 0; }

  Module *module() const { return module_; }

  gate_idx_t get_igate() const { return current_igate_; }
  void set_current_igate(gate_idx_t idx) const { current_igate_ = idx; }

  bess::PacketBatch *dead_batch() const { return &dead_batch_; }

  bess::PacketBatch *get_obatch(bess::OGate *ogate) const {
    return ogate_batch_[ogate->g_idx()];
  }

  void set_obatch(bess::OGate *ogate, bess::PacketBatch *batch) const {
    ogate_batch_[ogate->g_idx()] = batch;
  }

  bess::PacketBatch *get_ibatch(bess::IGate *igate) const {
    return igate_batch_[igate->g_idx()];
  }

  void set_ibatch(bess::IGate *igate, bess::PacketBatch *batch) const {
    igate_batch_[igate->g_idx()] = batch;
  }

  bess::LeafTrafficClass *GetTC() const { return c_; }

  struct task_result operator()(void) const;

  // Compute constraints for the pipeline starting at this task.
  placement_constraint GetSocketConstraints() const;

  // Add a worker to the set of workers that call this task.
  void AddActiveWorker(int wid) const;
};

#endif  // BESS_TASK_H_
