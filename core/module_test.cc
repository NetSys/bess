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

#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

namespace {

// Mocking out misc things  ------------------------------------------------

class AcmeModule : public Module {
 public:
  AcmeModule() : Module() {}

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 2;

  static const Commands cmds;

  CommandResponse Init(const bess::pb::EmptyArg &) { return CommandResponse(); }

  CommandResponse FooPb(const bess::pb::EmptyArg &) {
    n += 1;
    return CommandResponse();
  }

  int n = {};
};

const Commands AcmeModule::cmds = {{"foo", "EmptyArg",
                                    MODULE_CMD_FUNC(&AcmeModule::FooPb),
                                    Command::THREAD_UNSAFE}};

DEF_MODULE(AcmeModule, "acme_module", "foo bar");

class AcmeModuleWithTask : public Module {
 public:
  AcmeModuleWithTask() : Module() { is_task_ = true; }

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 3;

  CommandResponse Init(const bess::pb::EmptyArg &) { return CommandResponse(); }

  struct task_result RunTask(Context *, bess::PacketBatch *, void *) override {
    return task_result();
  }
};

DEF_MODULE(AcmeModuleWithTask, "acme_module_with_task", "foo bar");

// Simple harness for testing the Module class.
class ModuleTester : public ::testing::Test {
 protected:
  ModuleTester() : AcmeModule_singleton(), AcmeModuleWithTask_singleton() {}

  virtual void SetUp() {}

  virtual void TearDown() { ModuleGraph::DestroyAllModules(); }

  AcmeModule_class AcmeModule_singleton;
  AcmeModuleWithTask_class AcmeModuleWithTask_singleton;
};

Module *create_acme(const char *name, pb_error_t *perr) {
  const ModuleBuilder &builder =
      ModuleBuilder::all_module_builders().find("AcmeModule")->second;

  std::string mod_name;
  if (name) {
    if (ModuleGraph::GetAllModules().count(name)) {
      *perr = pb_errno(EEXIST);
      return nullptr;
    }
    mod_name = name;
  } else {
    mod_name = ModuleGraph::GenerateDefaultName(builder.class_name(),
                                                builder.name_template());
  }

  bess::pb::EmptyArg arg_;
  google::protobuf::Any arg;
  arg.PackFrom(arg_);

  Module *m = ModuleGraph::CreateModule(builder, mod_name, arg, perr);

  if (!m) {
    return nullptr;
  }
  EXPECT_EQ(0, perr->code());

  EXPECT_EQ("AcmeModule", builder.class_name());
  EXPECT_EQ("acme_module", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());
  EXPECT_EQ(1, builder.cmds().size());

  return m;
}

Module *create_acme_with_task(const char *name, pb_error_t *perr) {
  const ModuleBuilder &builder =
      ModuleBuilder::all_module_builders().find("AcmeModuleWithTask")->second;

  if (ModuleGraph::GetAllModules().count(name)) {
    *perr = pb_errno(EEXIST);
    return nullptr;
  }

  bess::pb::EmptyArg arg_;
  google::protobuf::Any arg;
  arg.PackFrom(arg_);

  Module *m = ModuleGraph::CreateModule(builder, name, arg, perr);
  if (!m) {
    return nullptr;
  }
  EXPECT_EQ(0, perr->code());

  EXPECT_EQ("AcmeModuleWithTask", builder.class_name());
  EXPECT_EQ("acme_module_with_task", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());

  return m;
}

// Check that new module classes are actually created correctly and stored in
// the table of module classes
TEST(ModuleBuilderTest, RegisterModuleClass) {
  ASSERT_EQ(0, ModuleBuilder::all_module_builders().count("AcmeModule"));

  AcmeModule_class AcmeModule_singleton;
  ASSERT_EQ(1, ModuleBuilder::all_module_builders().count("AcmeModule"));

  const ModuleBuilder &builder =
      ModuleBuilder::all_module_builders().find("AcmeModule")->second;

  EXPECT_EQ("AcmeModule", builder.class_name());
  EXPECT_EQ("acme_module", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());
  EXPECT_EQ(1, builder.NumIGates());
  EXPECT_EQ(2, builder.NumOGates());
  EXPECT_EQ(1, builder.cmds().size());

  ASSERT_EQ(0,
            ModuleBuilder::all_module_builders().count("AcmeModuleWithTask"));

  AcmeModuleWithTask_class AcmeModuleWithTask_singleton;
  ASSERT_EQ(1,
            ModuleBuilder::all_module_builders().count("AcmeModuleWithTask"));

  const ModuleBuilder &builder2 =
      ModuleBuilder::all_module_builders().find("AcmeModuleWithTask")->second;

  EXPECT_EQ("AcmeModuleWithTask", builder2.class_name());
  EXPECT_EQ("acme_module_with_task", builder2.name_template());
  EXPECT_EQ("foo bar", builder2.help_text());
  EXPECT_EQ(1, builder2.NumIGates());
  EXPECT_EQ(3, builder2.NumOGates());
  EXPECT_EQ(0, builder2.cmds().size());
}

// Check that module builders create modules correctly when given a name
TEST_F(ModuleTester, CreateModuleWithName) {
  pb_error_t perr;

  ASSERT_NE(nullptr, create_acme("bar", &perr));
  EXPECT_EQ(1, ModuleGraph::GetAllModules().size());
  ASSERT_EQ(nullptr, create_acme("bar", &perr));
  EXPECT_EQ(EEXIST, perr.code());
  EXPECT_EQ(1, ModuleGraph::GetAllModules().count("bar"));
}

// Check that module builders create modules with generated names
TEST_F(ModuleTester, CreateModuleGenerateName) {
  pb_error_t perr;

  ASSERT_NE(nullptr, create_acme(nullptr, &perr));
  EXPECT_EQ(1, ModuleGraph::GetAllModules().size());
  EXPECT_EQ(1, ModuleGraph::GetAllModules().count("acme_module0"));
  ASSERT_NE(nullptr, create_acme(nullptr, &perr));
  EXPECT_EQ(2, ModuleGraph::GetAllModules().size());
  EXPECT_EQ(1, ModuleGraph::GetAllModules().count("acme_module1"));
}

TEST_F(ModuleTester, RunCommand) {
  Module *m;
  pb_error_t perr;

  ASSERT_NE(nullptr, m = create_acme(nullptr, &perr));
  bess::pb::EmptyArg arg_;
  google::protobuf::Any arg;
  arg.PackFrom(arg_);

  CommandResponse response;

  for (int i = 0; i < 10; i++) {
    response = m->RunCommand("foo", arg);
    EXPECT_EQ(0, response.error().code());
  }
  EXPECT_EQ(10, (static_cast<AcmeModule *>(m))->n);

  response = m->RunCommand("bar", arg);
  EXPECT_EQ(ENOTSUP, response.error().code());
}

TEST_F(ModuleTester, ConnectModules) {
  pb_error_t perr;
  Module *m1, *m2;

  ASSERT_NE(nullptr, m1 = create_acme("m1", &perr));
  ASSERT_NE(nullptr, m2 = create_acme("m2", &perr));

  EXPECT_EQ(0, ModuleGraph::ConnectModules(m1, 0, m2, 0));
  EXPECT_EQ(1, m1->ogates().size());
  EXPECT_EQ(m2, m1->ogates()[0]->igate()->module());
  EXPECT_EQ(1, m2->igates().size());

  for (const auto &og : m2->igates()[0]->ogates_upstream()) {
    ASSERT_NE(nullptr, og);
    EXPECT_EQ(m1, og->module());
  }
}

TEST_F(ModuleTester, ResetModules) {
  pb_error_t perr;

  for (int i = 0; i < 10; i++) {
    ASSERT_NE(nullptr, create_acme(nullptr, &perr));
  }
  EXPECT_EQ(10, ModuleGraph::GetAllModules().size());

  ModuleGraph::DestroyAllModules();
  EXPECT_EQ(0, ModuleGraph::GetAllModules().size());
}

TEST(ModuleBuilderTest, GenerateDefaultNameTemplate) {
  std::string name1 = ModuleGraph::GenerateDefaultName("FooBar", "foo");
  EXPECT_EQ("foo0", name1);

  std::string name2 = ModuleGraph::GenerateDefaultName("FooBar", "");
  EXPECT_EQ("foo_bar0", name2);

  std::string name3 = ModuleGraph::GenerateDefaultName("FooABCBar", "");
  EXPECT_EQ("foo_abcbar0", name3);
}

TEST_F(ModuleTester, GenerateTCGraph) {
  pb_error_t perr;
  Module *t1, *t2, *t3, *t4, *m1, *m2, *m3;

  /* Test Topology           Expected TCGraph
   *       t2
   *      /
   *    m1                      t2
   *   /                       /
   * t1    t3                t1 -- t3
   *   \  /                    \
   *    m2                      t4
   *      \
   *       m3
   *         \
   *          t4
   */
  ASSERT_NE(nullptr, m1 = create_acme("m1", &perr));
  ASSERT_NE(nullptr, m2 = create_acme("m2", &perr));
  ASSERT_NE(nullptr, m3 = create_acme("m3", &perr));
  ASSERT_NE(nullptr, t1 = create_acme_with_task("t1", &perr));
  ASSERT_NE(nullptr, t2 = create_acme_with_task("t2", &perr));
  ASSERT_NE(nullptr, t3 = create_acme_with_task("t3", &perr));
  ASSERT_NE(nullptr, t4 = create_acme_with_task("t4", &perr));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(t1, 0, m1, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(t1, 1, m2, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m1, 0, t2, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m2, 0, t3, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m2, 1, m3, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m3, 0, t4, 0));

  ModuleGraph::UpdateTaskGraph();

  EXPECT_EQ(0, t1->parent_tasks().size());
  EXPECT_EQ(1, t2->parent_tasks().size());
  EXPECT_EQ(1, t3->parent_tasks().size());
  EXPECT_EQ(1, t4->parent_tasks().size());

  ASSERT_EQ(0, t1->children_overload());
  t2->SignalOverload();
  t3->SignalOverload();
  t4->SignalOverload();
  ASSERT_EQ(3, t1->children_overload());

  t2->SignalUnderload();
  t3->SignalUnderload();
  t4->SignalUnderload();
  ASSERT_EQ(0, t1->children_overload());

  ModuleGraph::DestroyModule(t1, true);
  ModuleGraph::CleanTaskGraph();
  ModuleGraph::UpdateTaskGraph();

  EXPECT_EQ(0, t2->parent_tasks().size());
  EXPECT_EQ(0, t3->parent_tasks().size());
  EXPECT_EQ(0, t4->parent_tasks().size());
}

TEST_F(ModuleTester, SetIGatePriority) {
  pb_error_t perr;
  Module *t1, *m1, *m2, *m3, *m4, *m5, *m6, *m7, *m8;

  /* Test Topology
   *        m7
   *      /   \     (backward from m6 -> m4)
   *    m1     \   ----------
   *   /        \ /          |
   * t1 -- m3 -- m4 -- m5 -- m6
   *   \  /                  /
   *    m2     -------------/
   *      \   /
   *       m8
   */
  ASSERT_NE(nullptr, t1 = create_acme_with_task("t1", &perr));
  ASSERT_NE(nullptr, m1 = create_acme("m1", &perr));
  ASSERT_NE(nullptr, m2 = create_acme("m2", &perr));
  ASSERT_NE(nullptr, m3 = create_acme("m3", &perr));
  ASSERT_NE(nullptr, m4 = create_acme("m4", &perr));
  ASSERT_NE(nullptr, m5 = create_acme("m5", &perr));
  ASSERT_NE(nullptr, m6 = create_acme("m6", &perr));
  ASSERT_NE(nullptr, m7 = create_acme("m7", &perr));
  ASSERT_NE(nullptr, m8 = create_acme("m8", &perr));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(t1, 0, m1, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(t1, 1, m2, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(t1, 2, m3, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m3, 0, m4, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m4, 0, m5, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m5, 0, m6, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m1, 0, m7, 0));
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m7, 0, m4, 0));  // merge
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m2, 0, m3, 0));  // merge
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m2, 1, m8, 0));  // split
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m8, 0, m6, 0));  // merge
  EXPECT_EQ(0, ModuleGraph::ConnectModules(m6, 0, m4, 0));  // loop

  ModuleGraph::UpdateTaskGraph();

  EXPECT_EQ(1, m1->igates()[0]->priority());
  EXPECT_EQ(1, m2->igates()[0]->priority());
  EXPECT_EQ(2, m3->igates()[0]->priority());  // takes the longest visit path
  EXPECT_EQ(3, m4->igates()[0]->priority());  // loop does not increase counts
  EXPECT_EQ(4, m5->igates()[0]->priority());
  EXPECT_EQ(5, m6->igates()[0]->priority());
  EXPECT_EQ(2, m7->igates()[0]->priority());
  EXPECT_EQ(2, m8->igates()[0]->priority());

  EXPECT_EQ(0 + 1, m1->igates()[0]->global_gate_index() +
                       m2->igates()[0]->global_gate_index());
  EXPECT_EQ(2 + 3 + 4, m3->igates()[0]->global_gate_index() +
                           m7->igates()[0]->global_gate_index() +
                           m8->igates()[0]->global_gate_index());
  EXPECT_EQ(5, m4->igates()[0]->global_gate_index());
  EXPECT_EQ(6, m5->igates()[0]->global_gate_index());
  EXPECT_EQ(7, m6->igates()[0]->global_gate_index());
}
}  // namespace
