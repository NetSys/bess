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

#include "worker_split.h"

const Commands WorkerSplit::cmds = {
    {"reset", "WorkerSplitArg", MODULE_CMD_FUNC(&WorkerSplit::CommandReset),
     Command::THREAD_UNSAFE}};

CommandResponse WorkerSplit::Init(const bess::pb::WorkerSplitArg &arg) {
  return CommandReset(arg);
}

CommandResponse WorkerSplit::CommandReset(const bess::pb::WorkerSplitArg &arg) {
  if (arg.worker_gates().empty()) {
    for (int i = 0; i < Worker::kMaxWorkers; i++) {
      gates_[i] = i;
    }
    return CommandSuccess();
  }

  for (size_t i = 0; i < Worker::kMaxWorkers; i++) {
    gates_[i] = -1;
  }

  for (auto it : arg.worker_gates()) {
    gate_idx_t ogate = it.second;
    if (ogate >= MAX_GATES) {
      return CommandFailure(EINVAL, "output gate must be less than %" PRIu16,
                            MAX_GATES);
    }
    gates_[it.first] = ogate;
  }

  return CommandSuccess();
}

void WorkerSplit::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  int gate = gates_[ctx->wid];
  if (gate >= 0) {
    RunChooseModule(ctx, gate, batch);
  } else {
    bess::Packet::Free(batch);
  }
}

void WorkerSplit::AddActiveWorker(int wid, const Task *t) {
  if (!HaveVisitedWorker(t)) {  // Have not already accounted for worker.
    active_workers_[wid] = true;
    visited_tasks_.push_back(t);
    // Only propagate workers downstream on ogate mapped to `wid`
    int g = gates_[wid];
    bess::OGate *ogate = (g < 0) ? nullptr : ogates()[g];
    if (ogate) {
      auto next = static_cast<Module *>(ogate->next());
      next->AddActiveWorker(wid, t);
    }
  }
}

ADD_MODULE(WorkerSplit, "ws",
           "send packets to output gate X, the id of current worker")
