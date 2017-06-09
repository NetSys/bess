#include "module.h"

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

  CommandResponse Init(const bess::pb::EmptyArg &) {
    return CommandFailure(42);
  }

  CommandResponse FooPb(const bess::pb::EmptyArg &) {
    n += 1;
    return CommandResponse();
  }

  int n = {};
};

const Commands AcmeModule::cmds = {
    {"foo", "EmptyArg", MODULE_CMD_FUNC(&AcmeModule::FooPb), 0}};

DEF_MODULE(AcmeModule, "acme_module", "foo bar");

class AcmeModuleWithTask : public Module {
 public:
  AcmeModuleWithTask() : Module() {
    is_task_ = true;
  }

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 2;

  CommandResponse Init(const bess::pb::EmptyArg &) {
    return CommandResponse();
  }

  struct task_result RunTask(void *) override {
    struct task_result ret;
    return ret;
  }
};

DEF_MODULE(AcmeModuleWithTask, "acme_module_with_task", "foo bar");

// Simple harness for testing the Module class.
class ModuleTester : public ::testing::Test {
 protected:
  ModuleTester() : AcmeModule_singleton(), AcmeModuleWithTask_singleton() {}

  virtual void SetUp() {}

  virtual void TearDown() { ModuleBuilder::DestroyAllModules(); }

  AcmeModule_class AcmeModule_singleton;
  AcmeModuleWithTask_class AcmeModuleWithTask_singleton;
};

int create_acme(const char *name, Module **m) {
  const ModuleBuilder &builder =
      ModuleBuilder::all_module_builders().find("AcmeModule")->second;

  std::string mod_name;
  if (name) {
    if (ModuleBuilder::all_modules().count(name)) {
      return EEXIST;
    }
    mod_name = name;
  } else {
    mod_name = ModuleBuilder::GenerateDefaultName(builder.class_name(),
                                                  builder.name_template());
  }

  *m = builder.CreateModule(mod_name, &bess::metadata::default_pipeline);

  bess::pb::EmptyArg arg_;
  google::protobuf::Any arg;
  arg.PackFrom(arg_);
  CommandResponse ret = (*m)->InitWithGenericArg(arg);
  EXPECT_EQ(42, ret.error().code());

  if (!ModuleBuilder::AddModule(*m)) {
    return 1;
  }

  EXPECT_EQ("AcmeModule", builder.class_name());
  EXPECT_EQ("acme_module", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());
  EXPECT_EQ(1, builder.cmds().size());

  return 0;
}

int create_acme_with_task(const char *name, Module **m) {
  const ModuleBuilder &builder =
      ModuleBuilder::all_module_builders().find("AcmeModuleWithTask")->second;

  if (ModuleBuilder::all_modules().count(name)) {
    return EEXIST;
  }

  *m = builder.CreateModule(name, &bess::metadata::default_pipeline);
  bess::pb::EmptyArg arg_;
  google::protobuf::Any arg;
  arg.PackFrom(arg_);
  CommandResponse ret = (*m)->InitWithGenericArg(arg);
  EXPECT_EQ(0, ret.error().code());

  if (!ModuleBuilder::AddModule(*m)) {
    return 1;
  }

  EXPECT_EQ("AcmeModuleWithTask", builder.class_name());
  EXPECT_EQ("acme_module_with_task", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());

  return 0;
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
  EXPECT_EQ(2, builder2.NumOGates());
  EXPECT_EQ(0, builder2.cmds().size());
}

// Check that module builders create modules correctly when given a name
TEST_F(ModuleTester, CreateModuleWithName) {
  Module *m1, *m2;

  EXPECT_EQ(0, create_acme("bar", &m1));
  ASSERT_NE(nullptr, m1);
  EXPECT_EQ(1, ModuleBuilder::all_modules().size());
  EXPECT_EQ(EEXIST, create_acme("bar", &m2));
  EXPECT_EQ(1, ModuleBuilder::all_modules().count("bar"));
}

// Check that module builders create modules with generated names
TEST_F(ModuleTester, CreateModuleGenerateName) {
  Module *m;

  EXPECT_EQ(0, create_acme(nullptr, &m));
  ASSERT_NE(nullptr, m);
  EXPECT_EQ(1, ModuleBuilder::all_modules().size());
  EXPECT_EQ(1, ModuleBuilder::all_modules().count("acme_module0"));
  EXPECT_EQ(0, create_acme(nullptr, &m));
  ASSERT_NE(nullptr, m);
  EXPECT_EQ(2, ModuleBuilder::all_modules().size());
  EXPECT_EQ(1, ModuleBuilder::all_modules().count("acme_module1"));
}

TEST_F(ModuleTester, RunCommand) {
  Module *m;

  EXPECT_EQ(0, create_acme(nullptr, &m));
  ASSERT_NE(nullptr, m);
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
  Module *m1, *m2;

  EXPECT_EQ(0, create_acme("m1", &m1));
  ASSERT_NE(nullptr, m1);
  EXPECT_EQ(0, create_acme("m2", &m2));
  ASSERT_NE(nullptr, m2);

  EXPECT_EQ(0, m1->ConnectModules(0, m2, 0));
  EXPECT_EQ(1, m1->ogates().size());
  EXPECT_EQ(m2, m1->ogates()[0]->igate()->module());
  EXPECT_EQ(1, m2->igates().size());

  for (const auto &og : m2->igates()[0]->ogates_upstream()) {
    ASSERT_NE(nullptr, og);
    EXPECT_EQ(m1, og->module());
  }
}

TEST_F(ModuleTester, ResetModules) {
  Module *m;

  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(0, create_acme(nullptr, &m));
    ASSERT_NE(nullptr, m);
  }
  EXPECT_EQ(10, ModuleBuilder::all_modules().size());

  ModuleBuilder::DestroyAllModules();
  EXPECT_EQ(0, ModuleBuilder::all_modules().size());
}

TEST(ModuleBuilderTest, GenerateDefaultNameTemplate) {
  std::string name1 = ModuleBuilder::GenerateDefaultName("FooBar", "foo");
  EXPECT_EQ("foo0", name1);

  std::string name2 = ModuleBuilder::GenerateDefaultName("FooBar", "");
  EXPECT_EQ("foo_bar0", name2);

  std::string name3 = ModuleBuilder::GenerateDefaultName("FooABCBar", "");
  EXPECT_EQ("foo_abcbar0", name3);
}

TEST_F(ModuleTester, GenerateTCGraph) {
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
  Module *t1, *t2, *t3, *t4, *m1, *m2, *m3;
  EXPECT_EQ(0, create_acme("m1", &m1));
  EXPECT_EQ(0, create_acme("m2", &m2));
  EXPECT_EQ(0, create_acme("m3", &m3));
  EXPECT_EQ(0, create_acme_with_task("t1", &t1));
  EXPECT_EQ(0, create_acme_with_task("t2", &t2));
  EXPECT_EQ(0, create_acme_with_task("t3", &t3));
  EXPECT_EQ(0, create_acme_with_task("t4", &t4));
  EXPECT_EQ(0, t1->ConnectModules(0, m1, 0));
  EXPECT_EQ(0, t1->ConnectModules(1, m2, 0));
  EXPECT_EQ(0, m1->ConnectModules(0, t2, 0));
  EXPECT_EQ(0, m2->ConnectModules(0, t3, 0));
  EXPECT_EQ(0, m2->ConnectModules(1, m3, 0));
  EXPECT_EQ(0, m3->ConnectModules(0, t4, 0));

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

  ModuleBuilder::DestroyModule(t1, true);
  EXPECT_EQ(0, t2->parent_tasks().size());
  EXPECT_EQ(0, t3->parent_tasks().size());
  EXPECT_EQ(0, t4->parent_tasks().size());
}
}  // namespace (unnamed)
