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
  static const gate_idx_t kNumOGates = 1;

  static const Commands cmds;

  pb_error_t Init(const bess::pb::EmptyArg &) { return pb_errno(42); }

  pb_cmd_response_t FooPb(const bess::pb::EmptyArg &) {
    n += 1;
    return pb_cmd_response_t();
  }

  int n = {};
};

const Commands AcmeModule::cmds = {
    {"foo", "EmptyArg", MODULE_CMD_FUNC(&AcmeModule::FooPb), 0}};

// Simple harness for testing the Module class.
class ModuleTester : public ::testing::Test {
 protected:
  virtual void SetUp() {
    ADD_MODULE(AcmeModule, "acme_module", "foo bar");
    ASSERT_TRUE(__module__AcmeModule);
  }

  virtual void TearDown() {
    ModuleBuilder::DestroyAllModules();
    ModuleBuilder::all_module_builders_holder(true);
  }
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
  pb_error_t err = (*m)->InitWithGenericArg(arg);
  EXPECT_EQ(42, err.err());

  ModuleBuilder::AddModule(*m);

  EXPECT_EQ("AcmeModule", builder.class_name());
  EXPECT_EQ("acme_module", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());
  EXPECT_EQ(1, builder.cmds().size());

  return 0;
}

// Check that new module classes are actually created correctly and stored in
// the table of module classes
TEST(ModuleBuilderTest, RegisterModuleClass) {
  size_t num_builders = ModuleBuilder::all_module_builders().size();
  ADD_MODULE(AcmeModule, "acme_module", "foo bar");
  ASSERT_TRUE(__module__AcmeModule);

  EXPECT_EQ(num_builders + 1, ModuleBuilder::all_module_builders().size());
  ASSERT_EQ(1, ModuleBuilder::all_module_builders().count("AcmeModule"));

  const ModuleBuilder &builder =
      ModuleBuilder::all_module_builders().find("AcmeModule")->second;

  EXPECT_EQ("AcmeModule", builder.class_name());
  EXPECT_EQ("acme_module", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());
  EXPECT_EQ(1, builder.NumIGates());
  EXPECT_EQ(1, builder.NumOGates());
  EXPECT_EQ(1, builder.cmds().size());

  ModuleBuilder::all_module_builders_holder(true);
}

TEST(ModuleBuilderTest, GenerateDefaultNameTemplate) {
  std::string name1 = ModuleBuilder::GenerateDefaultName("FooBar", "foo");
  EXPECT_EQ("foo0", name1);

  std::string name2 = ModuleBuilder::GenerateDefaultName("FooBar", "");
  EXPECT_EQ("foo_bar0", name2);

  std::string name3 = ModuleBuilder::GenerateDefaultName("FooABCBar", "");
  EXPECT_EQ("foo_abcbar0", name3);
}

// Check that module builders create modules correctly when given a name
TEST_F(ModuleTester, CreateModuleWithName) {
  Module *m1, *m2;
  ADD_MODULE(AcmeModule, "acme_module", "foo bar");
  ASSERT_TRUE(__module__AcmeModule);

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

  pb_cmd_response_t response;

  for (int i = 0; i < 10; i++) {
    response = m->RunCommand("foo", arg);
    EXPECT_EQ(0, response.error().err());
  }
  EXPECT_EQ(10, (static_cast<AcmeModule *>(m))->n);

  response = m->RunCommand("bar", arg);
  EXPECT_EQ(ENOTSUP, response.error().err());
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

}  // namespace (unnamed)
