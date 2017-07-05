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

#include <benchmark/benchmark.h>
#include <glog/logging.h>

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

DEF_MODULE(DummySourceModule, "src", "the most sophisticated modue ever");
DEF_MODULE(DummyRelayModule, "relay", "the most sophisticated modue ever");

// Simple harness for testing the Module class.
class ModuleFixture : public benchmark::Fixture {
 protected:
  ModuleFixture()
      : DummySourceModule_singleton(), DummyRelayModule_singleton() {}
  void SetUp(benchmark::State &state) override {
    const int chain_length = state.range(0);

    const auto &builders = ModuleBuilder::all_module_builders();
    const auto &builder_src = builders.find("DummySourceModule")->second;
    const auto &builder_relay = builders.find("DummyRelayModule")->second;
    Module *last;

    src_ = builder_src.CreateModule("src0", &bess::metadata::default_pipeline);
    ModuleBuilder::AddModule(src_);

    last = src_;

    for (int i = 0; i < chain_length; i++) {
      Module *relay = builder_relay.CreateModule(
          "relay" + std::to_string(i), &bess::metadata::default_pipeline);
      ModuleBuilder::AddModule(relay);

      relays.push_back(relay);
      int ret = last->ConnectModules(0, relay, 0);
      DCHECK_EQ(ret, 0);
      last = relay;
    }
  }

  void TearDown(benchmark::State &) override {
    ModuleBuilder::DestroyAllModules();
  }

  Module *src_;
  std::vector<Module *> relays;
  DummySourceModule_class DummySourceModule_singleton;
  DummyRelayModule_class DummyRelayModule_singleton;
};

}  // namespace (unnamed)

BENCHMARK_DEFINE_F(ModuleFixture, Chain)(benchmark::State &state) {
  const size_t batch_size = bess::PacketBatch::kMaxBurst;

  Task t(src_, reinterpret_cast<void *>(batch_size), nullptr);

  while (state.KeepRunning()) {
    struct task_result ret = t();
    DCHECK_EQ(ret.packets, batch_size);
  }

  state.SetItemsProcessed(state.iterations() * batch_size);
}

BENCHMARK_REGISTER_F(ModuleFixture, Chain)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(5)
    ->Arg(6)
    ->Arg(7)
    ->Arg(8)
    ->Arg(9)
    ->Arg(10);

BENCHMARK_MAIN()
