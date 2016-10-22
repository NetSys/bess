#include "module.h"

#include <gtest/gtest.h>

// Mocking out misc things  ------------------------------------------------

__thread struct worker_context ctx = {};

struct task *task_create(Module *m, void *arg) {
  return nullptr;
}

void task_destroy(struct task *t) {}

void task_attach(struct task *t, struct tc *c) {}
void task_detach(struct task *t) {}

struct snobj *snobj_str(const char *s) { return nullptr; }
struct snobj *snobj_nil() { return nullptr; }

struct snobj *snobj_err_details(int err, struct snobj *details, const char *fmt,
                                ...) {
  return nullptr;
}

// -------------------------------------------------------------------------

class AcmeModule : public Module {
  public:
    AcmeModule() : Module() {}

    static const gate_idx_t kNumIGates = 1;
    static const gate_idx_t kNumOGates = 1;

    static const Commands<Module> cmds;

    void Foo(struct snobj *arg) { n += 1; }

    int n = {};
};

const Commands<Module> AcmeModule::cmds = {
  {"foo", MODULE_FUNC &AcmeModule::Foo, 0}
};

// Simple test harness.
class ModuleTester : public ::testing::Test {
  protected:
    virtual void SetUp() {
      ADD_MODULE(AcmeModule, "acme_module", "foo bar")
        ASSERT_TRUE(__module__AcmeModule);
      const ModuleBuilder &builder = ModuleBuilder::all_module_builders().find("AcmeModule")->second;
      EXPECT_EQ("AcmeModule", builder.class_name());
      EXPECT_EQ("acme_module", builder.name_template());
      EXPECT_EQ("foo bar", builder.help_text());
      EXPECT_EQ(1, builder.cmds().size());
    }

    virtual void TearDown() {
      ModuleBuilder::DestroyAllModules();
      ModuleBuilder::all_module_builders_holder(true);
    }
};

// Check that new module classes are actually created correctly and stored in
// the table of module classes
TEST(ModuleBuilderTest, RegisterModuleClass) {
  ADD_MODULE(AcmeModule, "acme_module", "foo bar")
  ASSERT_TRUE(__module__AcmeModule);

  EXPECT_EQ(1, ModuleBuilder::all_module_builders().size());
  EXPECT_EQ(1, ModuleBuilder::all_module_builders().count("AcmeModule"));

  const ModuleBuilder &builder = ModuleBuilder::all_module_builders().find("AcmeModule")->second;
  EXPECT_EQ("AcmeModule", builder.class_name());
  EXPECT_EQ("acme_module", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());
  EXPECT_EQ(1, builder.cmds().size());

  ModuleBuilder::all_module_builders_holder(true);
}

int create_acme(const char *name, Module **m) {
  EXPECT_EQ(1, ModuleBuilder::all_module_builders().size());
  EXPECT_EQ(1, ModuleBuilder::all_module_builders().count("AcmeModule"));

  const ModuleBuilder &builder = ModuleBuilder::all_module_builders().find("AcmeModule")->second;

  std::string mod_name;
  if (name) {
    if (ModuleBuilder::all_modules().count(name)) {
      return EEXIST;
    }
    mod_name = name;
  } else {
    mod_name = ModuleBuilder::GenerateDefaultName(builder.class_name(), builder.name_template());
  }

  *m = builder.CreateModule(mod_name);
  builder.AddModule(*m); 

  EXPECT_EQ("AcmeModule", builder.class_name());
  EXPECT_EQ("acme_module", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());
  EXPECT_EQ(1, builder.cmds().size());

  return 0;
}

// Check that module builders create modules correctly when given a name
TEST(ModuleBuilderTest, CreateModuleWithName) {
  Module *m1, *m2;
  ADD_MODULE(AcmeModule, "acme_module", "foo bar")
  ASSERT_TRUE(__module__AcmeModule);

  EXPECT_EQ(0, create_acme("bar", &m1));
  ASSERT_NE(nullptr, m1);
  EXPECT_EQ(1, ModuleBuilder::all_modules().size());
  EXPECT_EQ(EEXIST, create_acme("bar", &m2));
  EXPECT_EQ(1, ModuleBuilder::all_modules().count("bar"));

  ModuleBuilder::DestroyAllModules();
  ModuleBuilder::all_module_builders_holder(true);
}

// Check that module builders create modules with generated names
TEST(ModuleBuilderTest, CreateModuleGenerateName) {
  Module *m;
  ADD_MODULE(AcmeModule, "acme_module", "foo bar")
  ASSERT_TRUE(__module__AcmeModule);

  EXPECT_EQ(0, create_acme(nullptr, &m));
  ASSERT_NE(nullptr, m);
  EXPECT_EQ(1, ModuleBuilder::all_modules().size());
  EXPECT_EQ(1, ModuleBuilder::all_modules().count("acme_module0"));
  EXPECT_EQ(0, create_acme(nullptr, &m));
  ASSERT_NE(nullptr, m);
  EXPECT_EQ(2, ModuleBuilder::all_modules().size());
  EXPECT_EQ(1, ModuleBuilder::all_modules().count("acme_module1"));

  ModuleBuilder::DestroyAllModules();
  ModuleBuilder::all_module_builders_holder(true);
}

TEST(ModuleBuilderTest, AcmeRunCommand) {
  Module *m;
  ADD_MODULE(AcmeModule, "acme_module", "foo bar")
  ASSERT_TRUE(__module__AcmeModule);

  EXPECT_EQ(0, create_acme(nullptr, &m));
  ASSERT_NE(nullptr, m);
  for (int i = 0; i < 10; i++) {
    m->RunCommand("foo", nullptr);
  }
  EXPECT_EQ(10, ((AcmeModule*)m)->n);

  ModuleBuilder::DestroyAllModules();
  ModuleBuilder::all_module_builders_holder(true);
}

// XXX: ProcessBatch() test?
