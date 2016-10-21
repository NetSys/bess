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

// -------------------------------------------------------------------------

struct snobj *snobj_str(const char *s) { return nullptr; }
struct snobj *snobj_nil() { return nullptr; }

class AcmeModule : public Module {
  public:
    AcmeModule() : Module() {}

    virtual struct snobj *Init(struct snobj *arg) {
      return nullptr;
    }

    virtual void Deinit() {
    }

    virtual struct task_result RunTask(void *arg) {
      struct task_result ret = {
        .packets = 0,
        .bits = 0,
      };
      return ret;
    }

    virtual void ProcessBatch(struct pkt_batch *batch) {
    }

    static const gate_idx_t kNumIGates = 1;
    static const gate_idx_t kNumOGates = 1;

    static const Commands<Module> cmds;
};

const Commands<Module> AcmeModule::cmds = {};

// Check that new module classes are actually created and stored in the table of
// module classes
TEST(ModuleBuilderTest, RegisterModuleClass) {
  ADD_MODULE(AcmeModule, "acme_module", "foo bar")

  ASSERT_TRUE(__module__AcmeModule);

  EXPECT_EQ(ModuleBuilder::all_module_builders().size(), 1);
  EXPECT_EQ(ModuleBuilder::all_module_builders().count("AcmeModule"), 1);

  const ModuleBuilder &builder = ModuleBuilder::all_module_builders().find("AcmeModule")->second;
  EXPECT_EQ("AcmeModule", builder.class_name());
  EXPECT_EQ("acme_module", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());

  ModuleBuilder::all_module_builders_holder(true);
}
