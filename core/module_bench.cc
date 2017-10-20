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

#include "module.h"
#include "module_graph.h"

#include <benchmark/benchmark.h>
#include <glog/logging.h>
#include <math.h>

#include "traffic_class.h"

namespace {

class DummySourceModule : public Module {
 public:
  struct task_result RunTask(void *arg) override;
};

[[gnu::noinline]] struct task_result DummySourceModule::RunTask(void *arg) {
  const uint32_t batch_size = reinterpret_cast<size_t>(arg);
  bess::PacketBatch batch;

  bess::Packet pkts[bess::PacketBatch::kMaxBurst];

  batch.clear();
  for (size_t i = 0; i < batch_size; i++) {
    bess::Packet *pkt = &pkts[i];

    // this fake packet must not be freed
    pkt->set_refcnt(2);

    // not chained
    pkt->set_next(nullptr);

    batch.add(pkt);
  }

  RunNextModule(&batch);

  return {.block = false, .packets = batch_size, .bits = 0};
}

class DummyRelayModule : public Module {
 public:
  void ProcessBatch(bess::PacketBatch *batch) override;
};

[[gnu::noinline]] void DummyRelayModule::ProcessBatch(
    bess::PacketBatch *batch) {
  RunNextModule(batch);
}

class DummySplitModule : public Module {
 public:
  void ProcessBatch(bess::PacketBatch *batch) override;
  void SetSplitCnt(int ngates);

 private:
  int ngates_;
  int current_gate_;
};

[[gnu::noinline]] void DummySplitModule::ProcessBatch(
    bess::PacketBatch *batch) {
  gate_idx_t ogates[bess::PacketBatch::kMaxBurst];

  for (int i = 0; i < batch->cnt(); i++) {
    ogates[i] = current_gate_;
    if (++current_gate_ >= ngates_) {
      current_gate_ = 0;
    }
  }

  RunSplit(ogates, batch);
}

void DummySplitModule::SetSplitCnt(int ngates) {
  ngates_ = ngates;
  current_gate_ = 0;
}

class DummySinkModule : public Module {
 public:
  void ProcessBatch(bess::PacketBatch *batch) override;
};

[[gnu::noinline]] void DummySinkModule::ProcessBatch(bess::PacketBatch *) {
  // Do not free, since packets are in stack
}

DEF_MODULE(DummySourceModule, "src", "the most sophisticated modue ever");
DEF_MODULE(DummyRelayModule, "relay", "the most sophisticated modue ever");
DEF_MODULE(DummySplitModule, "split", "Split packets into the number of ogate");
DEF_MODULE(DummySinkModule, "sink", "packet sink");

// Simple harness for testing the Module class.
class ChainFixture : public benchmark::Fixture {
 protected:
  ChainFixture()
      : DummySourceModule_singleton(),
        DummyRelayModule_singleton(),
        DummySinkModule_singleton() {}

  void SetUp(benchmark::State &state) override {
    const int chain_length = state.range(0);

    const auto &builders = ModuleBuilder::all_module_builders();
    const auto &builder_src = builders.find("DummySourceModule")->second;
    const auto &builder_relay = builders.find("DummyRelayModule")->second;
    const auto &builder_sink = builders.find("DummySinkModule")->second;
    Module *last;

    bess::pb::EmptyArg arg_;
    google::protobuf::Any arg;
    arg.PackFrom(arg_);
    pb_error_t perr;

    src_ = ModuleGraph::CreateModule(builder_src, "src", arg, &perr);
    Module *sink = ModuleGraph::CreateModule(builder_sink, "sink", arg, &perr);

    last = src_;

    for (int i = 0; i < chain_length; i++) {
      Module *relay = ModuleGraph::CreateModule(
          builder_relay, "relay" + std::to_string(i), arg, &perr);

      int ret = last->ConnectModules(0, relay, 0);
      DCHECK_EQ(ret, 0);
      last = relay;
    }

    int ret = last->ConnectModules(0, sink, 0);
    DCHECK_EQ(ret, 0);
  }

  void TearDown(benchmark::State &) override {
    ModuleGraph::DestroyAllModules();
  }

  Module *src_;
  DummySourceModule_class DummySourceModule_singleton;
  DummyRelayModule_class DummyRelayModule_singleton;
  DummySinkModule_class DummySinkModule_singleton;
};

class SplitFixture : public benchmark::Fixture {
 protected:
  SplitFixture()
      : DummySourceModule_singleton(),
        DummyRelayModule_singleton(),
        DummySplitModule_singleton(),
        DummySinkModule_singleton() {}

  void SetUp(benchmark::State &state) override {
    const int split_cnt = state.range(0);

    const auto &builders = ModuleBuilder::all_module_builders();
    const auto &builder_src = builders.find("DummySourceModule")->second;
    const auto &builder_relay = builders.find("DummyRelayModule")->second;
    const auto &builder_split = builders.find("DummySplitModule")->second;
    const auto &builder_sink = builders.find("DummySinkModule")->second;

    bess::pb::EmptyArg arg_;
    google::protobuf::Any arg;
    arg.PackFrom(arg_);
    pb_error_t perr;

    src_ = ModuleGraph::CreateModule(builder_src, "src", arg, &perr);
    Module *split =
        ModuleGraph::CreateModule(builder_split, "split", arg, &perr);
    static_cast<DummySplitModule *>(split)->SetSplitCnt(split_cnt);

    int ret = src_->ConnectModules(0, split, 0);
    DCHECK_EQ(ret, 0);

    for (int i = 0; i < split_cnt; i++) {
      Module *relay_split = ModuleGraph::CreateModule(
          builder_relay, "relay_sp" + std::to_string(i), arg, &perr);
      Module *relay_chain = ModuleGraph::CreateModule(
          builder_relay, "relay_ch" + std::to_string(i), arg, &perr);
      Module *sink = ModuleGraph::CreateModule(
          builder_sink, "sink" + std::to_string(i), arg, &perr);

      ret = split->ConnectModules(i, relay_split, 0);
      DCHECK_EQ(ret, 0);
      ret = relay_split->ConnectModules(0, relay_chain, 0);
      DCHECK_EQ(ret, 0);
      ret = relay_chain->ConnectModules(0, sink, 0);
      DCHECK_EQ(ret, 0);
    }
  }

  void TearDown(benchmark::State &) override {
    ModuleGraph::DestroyAllModules();
  }

  Module *src_;
  DummySourceModule_class DummySourceModule_singleton;
  DummyRelayModule_class DummyRelayModule_singleton;
  DummySplitModule_class DummySplitModule_singleton;
  DummySinkModule_class DummySinkModule_singleton;
};

class MergeFixture : public benchmark::Fixture {
 protected:
  MergeFixture()
      : DummySourceModule_singleton(),
        DummyRelayModule_singleton(),
        DummySplitModule_singleton(),
        DummySinkModule_singleton() {}

  void SetUp(benchmark::State &state) override {
    const int split_cnt = state.range(0);

    const auto &builders = ModuleBuilder::all_module_builders();
    const auto &builder_src = builders.find("DummySourceModule")->second;
    const auto &builder_relay = builders.find("DummyRelayModule")->second;
    const auto &builder_split = builders.find("DummySplitModule")->second;
    const auto &builder_sink = builders.find("DummySinkModule")->second;

    bess::pb::EmptyArg arg_;
    google::protobuf::Any arg;
    arg.PackFrom(arg_);
    pb_error_t perr;

    src_ = ModuleGraph::CreateModule(builder_src, "src", arg, &perr);
    Module *split =
        ModuleGraph::CreateModule(builder_split, "split", arg, &perr);
    Module *relay_merge =
        ModuleGraph::CreateModule(builder_split, "merge", arg, &perr);
    Module *sink = ModuleGraph::CreateModule(builder_sink, "sink", arg, &perr);

    static_cast<DummySplitModule *>(split)->SetSplitCnt(split_cnt);

    int ret = src_->ConnectModules(0, split, 0);
    DCHECK_EQ(ret, 0);

    for (int i = 0; i < split_cnt; i++) {
      Module *relay_split = ModuleGraph::CreateModule(
          builder_relay, "relay_sp" + std::to_string(i), arg, &perr);

      ret = split->ConnectModules(i, relay_split, 0);
      DCHECK_EQ(ret, 0);
      ret = relay_split->ConnectModules(0, relay_merge, 0);
      DCHECK_EQ(ret, 0);
    }
    ret = relay_merge->ConnectModules(0, sink, 0);
    DCHECK_EQ(ret, 0);
  }

  void TearDown(benchmark::State &) override {
    ModuleGraph::DestroyAllModules();
  }

  Module *src_;
  DummySourceModule_class DummySourceModule_singleton;
  DummyRelayModule_class DummyRelayModule_singleton;
  DummySplitModule_class DummySplitModule_singleton;
  DummySinkModule_class DummySinkModule_singleton;
};

class ComplexSplitFixture : public benchmark::Fixture {
 protected:
  ComplexSplitFixture()
      : DummySourceModule_singleton(),
        DummySplitModule_singleton(),
        DummySinkModule_singleton() {}

  void SetUp(benchmark::State &state) override {
    const int child_cnt = state.range(0);
    const int tree_depth = state.range(1);

    const auto &builders = ModuleBuilder::all_module_builders();
    const auto &builder_src = builders.find("DummySourceModule")->second;
    const auto &builder_split = builders.find("DummySplitModule")->second;
    const auto &builder_sink = builders.find("DummySinkModule")->second;

    bess::pb::EmptyArg arg_;
    google::protobuf::Any arg;
    arg.PackFrom(arg_);
    pb_error_t perr;

    int split_module_cnt =
        (pow(child_cnt, tree_depth + 1) - 1) / (child_cnt - 1);
    Module *split[split_module_cnt];

    src_ = ModuleGraph::CreateModule(builder_src, "src", arg, &perr);
    split[0] = ModuleGraph::CreateModule(builder_split, "split0", arg, &perr);

    int ret = src_->ConnectModules(0, split[0], 0);
    DCHECK_EQ(ret, 0);

    int parent_idx = 0;
    int child_idx = 1;

    // Split: parents(1) -> child(child_cnt)
    for (int i = 0; i < tree_depth; i++) {
      int parents_cnt = pow(child_cnt, i);

      for (int j = 0; j < parents_cnt; j++) {
        Module *parent = split[parent_idx];
        static_cast<DummySplitModule *>(parent)->SetSplitCnt(child_cnt);

        for (int k = 0; k < child_cnt; k++) {
          Module *child = split[child_idx] = ModuleGraph::CreateModule(
              builder_split, "split" + std::to_string(child_idx), arg, &perr);
          ret = parent->ConnectModules(k, child, 0);
          DCHECK_EQ(ret, 0);
          child_idx++;
        }
        parent_idx++;
      }
    }

    // Free packets: leaf -> sink
    int leaf_cnt = pow(child_cnt, tree_depth);
    int leaf_idx = parent_idx;
    for (int i = 0; i < leaf_cnt; i++) {
      Module *leaf = split[leaf_idx];
      Module *sink = ModuleGraph::CreateModule(
          builder_sink, "sink" + std::to_string(i), arg, &perr);
      ret = leaf->ConnectModules(0, sink, 0);
      DCHECK_EQ(ret, 0);
      leaf_idx++;
    }
  }

  void TearDown(benchmark::State &) override {
    ModuleGraph::DestroyAllModules();
  }

  Module *src_;
  DummySourceModule_class DummySourceModule_singleton;
  DummyRelayModule_class DummyRelayModule_singleton;
  DummySplitModule_class DummySplitModule_singleton;
  DummySinkModule_class DummySinkModule_singleton;
};

class ComplexMergeFixture : public benchmark::Fixture {
 protected:
  ComplexMergeFixture()
      : DummySourceModule_singleton(),
        DummyRelayModule_singleton(),
        DummySplitModule_singleton(),
        DummySinkModule_singleton() {}

  void SetUp(benchmark::State &state) override {
    const int child_cnt = state.range(0);
    const int tree_depth = state.range(1);

    const auto &builders = ModuleBuilder::all_module_builders();
    const auto &builder_src = builders.find("DummySourceModule")->second;
    const auto &builder_relay = builders.find("DummyRelayModule")->second;
    const auto &builder_split = builders.find("DummySplitModule")->second;
    const auto &builder_sink = builders.find("DummySinkModule")->second;

    bess::pb::EmptyArg arg_;
    google::protobuf::Any arg;
    arg.PackFrom(arg_);
    pb_error_t perr;

    int split_module_cnt = (pow(child_cnt, tree_depth) - 1) / (child_cnt - 1);
    int merge_module_cnt =
        (pow(child_cnt, tree_depth + 1) - 1) / (child_cnt - 1);
    Module *split[split_module_cnt];
    Module *merge[merge_module_cnt];

    src_ = ModuleGraph::CreateModule(builder_src, "src", arg, &perr);
    split[0] = ModuleGraph::CreateModule(builder_split, "split0", arg, &perr);

    int ret = src_->ConnectModules(0, split[0], 0);
    DCHECK_EQ(ret, 0);

    int parent_idx = 0;
    int child_idx = 1;

    // Split: parents(1) -> child(child_cnt)
    for (int i = 0; i < tree_depth - 1; i++) {
      int parents_cnt = pow(child_cnt, i);

      for (int j = 0; j < parents_cnt; j++) {
        Module *parent = split[parent_idx];
        static_cast<DummySplitModule *>(parent)->SetSplitCnt(child_cnt);

        for (int k = 0; k < child_cnt; k++) {
          Module *child = split[child_idx] = ModuleGraph::CreateModule(
              builder_split, "split" + std::to_string(child_idx), arg, &perr);
          ret = parent->ConnectModules(k, child, 0);
          DCHECK_EQ(ret, 0);
          child_idx++;
        }
        parent_idx++;
      }
    }

    int parents_cnt = pow(child_cnt, tree_depth - 1);
    child_idx = parent_idx + parents_cnt;
    for (int i = 0; i < parents_cnt; i++) {
      Module *parent = split[parent_idx];
      for (int k = 0; k < child_cnt; k++) {
        merge[child_idx] = ModuleGraph::CreateModule(
            builder_relay, "merge" + std::to_string(child_idx), arg, &perr);
        ret = parent->ConnectModules(0, merge[child_idx], 0);
        DCHECK_EQ(ret, 0);
        child_idx++;
      }
      parent_idx++;
    }

    // Merge: parents(child_cnt) -> child(1)
    int tmp_idx = parent_idx;
    parent_idx = child_idx - 1;
    child_idx = tmp_idx - 1;

    while (child_idx >= 0) {
      merge[child_idx] = ModuleGraph::CreateModule(
          builder_relay, "merge" + std::to_string(child_idx), arg, &perr);

      for (int i = 0; i < child_cnt; i++) {
        Module *parent = merge[parent_idx];
        ret = parent->ConnectModules(0, merge[child_idx], 0);
        DCHECK_EQ(ret, 0);
        parent_idx--;
      }
      child_idx--;
    }

    Module *sink = ModuleGraph::CreateModule(builder_sink, "sink", arg, &perr);
    ret = merge[0]->ConnectModules(0, sink, 0);
    DCHECK_EQ(ret, 0);
  }

  void TearDown(benchmark::State &) override {
    ModuleGraph::DestroyAllModules();
  }

  Module *src_;
  DummySourceModule_class DummySourceModule_singleton;
  DummyRelayModule_class DummyRelayModule_singleton;
  DummySplitModule_class DummySplitModule_singleton;
  DummySinkModule_class DummySinkModule_singleton;
};

}  // namespace (unnamed)

BENCHMARK_DEFINE_F(ChainFixture, Chain)(benchmark::State &state) {
  const size_t batch_size = bess::PacketBatch::kMaxBurst;

  Task t(src_, reinterpret_cast<void *>(batch_size));

  while (state.KeepRunning()) {
    struct task_result ret = t();
    DCHECK_EQ(ret.packets, batch_size);
  }

  state.SetItemsProcessed(state.iterations() * batch_size);
}

BENCHMARK_DEFINE_F(SplitFixture, Split)(benchmark::State &state) {
  const size_t batch_size = bess::PacketBatch::kMaxBurst;

  Task t(src_, reinterpret_cast<void *>(batch_size));

  while (state.KeepRunning()) {
    struct task_result ret = t();
    DCHECK_EQ(ret.packets, batch_size);
  }

  state.SetItemsProcessed(state.iterations() * batch_size);
}

BENCHMARK_DEFINE_F(MergeFixture, Merge)(benchmark::State &state) {
  const size_t batch_size = bess::PacketBatch::kMaxBurst;

  Task t(src_, reinterpret_cast<void *>(batch_size));

  while (state.KeepRunning()) {
    struct task_result ret = t();
    DCHECK_EQ(ret.packets, batch_size);
  }

  state.SetItemsProcessed(state.iterations() * batch_size);
}

BENCHMARK_DEFINE_F(ComplexSplitFixture, ComplexSplit)(benchmark::State &state) {
  const size_t batch_size = bess::PacketBatch::kMaxBurst;

  Task t(src_, reinterpret_cast<void *>(batch_size));

  while (state.KeepRunning()) {
    struct task_result ret = t();
    DCHECK_EQ(ret.packets, batch_size);
  }

  state.SetItemsProcessed(state.iterations() * batch_size);
}

BENCHMARK_DEFINE_F(ComplexMergeFixture, ComplexMerge)(benchmark::State &state) {
  const size_t batch_size = bess::PacketBatch::kMaxBurst;

  Task t(src_, reinterpret_cast<void *>(batch_size));

  while (state.KeepRunning()) {
    struct task_result ret = t();
    DCHECK_EQ(ret.packets, batch_size);
  }

  state.SetItemsProcessed(state.iterations() * batch_size);
}

BENCHMARK_REGISTER_F(ChainFixture, Chain)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32);

BENCHMARK_REGISTER_F(SplitFixture, Split)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32);

BENCHMARK_REGISTER_F(MergeFixture, Merge)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32);

BENCHMARK_REGISTER_F(ComplexSplitFixture, ComplexSplit)
    ->Args({2, 2})
    ->Args({2, 3})
    ->Args({2, 4})
    ->Args({2, 5})
    ->Args({3, 2})
    ->Args({3, 3})
    ->Args({3, 4})
    ->Args({3, 5});

BENCHMARK_REGISTER_F(ComplexMergeFixture, ComplexMerge)
    ->Args({2, 2})
    ->Args({2, 3})
    ->Args({2, 4})
    ->Args({2, 5})
    ->Args({3, 2})
    ->Args({3, 3})
    ->Args({3, 4})
    ->Args({3, 5});

BENCHMARK_MAIN()
