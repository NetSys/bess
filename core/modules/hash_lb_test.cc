#include "hash_lb.h"

#include <gtest/gtest.h>

// Mocking out misc things  ------------------------------------------------

// -------------------------------------------------------------------------

namespace {
void snobj_free_and_null(struct snobj **obj) {
  snobj_free(*obj);
  *obj = nullptr;
}
}
namespace bess {
namespace modules {

class HashLBTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    ADD_MODULE(HashLB, "hash_lb",
               "splits packets on a flow basis with L2/L3/L4 header fields")
    ASSERT_TRUE(__module__HashLB);
    const ModuleBuilder &builder =
        ModuleBuilder::all_module_builders().find("HashLB")->second;

    std::string mod_name = ModuleBuilder::GenerateDefaultName(
        builder.class_name(), builder.name_template());

    hl0 = reinterpret_cast<HashLB *>(
        builder.CreateModule(mod_name, &default_pipeline));
    builder.AddModule(reinterpret_cast<Module *>(hl0));
    ASSERT_NE(nullptr, hl0);
  }

  virtual void TearDown() {
    if (arg) {
      snobj_free(arg);
    }
    if (ret) {
      snobj_free(ret);
    }
    ModuleBuilder::all_module_builders_holder(true);
  }

  bool SetMode(const char *mode) {
    arg = snobj_str(mode);
    ret = hl0->RunCommand("set_mode", arg);
    ::snobj_free_and_null(&arg);
    return ret == nullptr;
  }

  bool Init(int n_gates, struct snobj *mode) {
    arg = snobj_map();
    if (n_gates >= 0) {
      snobj_map_set(arg, "gates", snobj_int(n_gates));
    }
    if (mode) {
      snobj_map_set(arg, "mode", mode);
    }
    ret = hl0->Init(arg);
    return ret == nullptr;
  }

  HashLB *hl0;
  struct snobj *arg;
  struct snobj *ret;
};

TEST_F(HashLBTest, SetModeFailsNoArgs) {
  arg = snobj_nil();
  ret = hl0->RunCommand("set_mode", arg);
  ASSERT_NE(nullptr, ret);
}

// Check that we can only set mode to reasonable values
TEST_F(HashLBTest, SetMode) {
  ASSERT_TRUE(SetMode("l2"));
  ASSERT_EQ(LB_L2, hl0->mode_);

  ASSERT_TRUE(SetMode("l3"));
  ASSERT_EQ(LB_L3, hl0->mode_);

  ASSERT_TRUE(SetMode("l4"));
  ASSERT_EQ(LB_L4, hl0->mode_);

  ASSERT_FALSE(SetMode("banana"));
}

TEST_F(HashLBTest, SetGatesFailsNoArgs) {
  arg = snobj_nil();
  ret = hl0->RunCommand("set_gates", arg);
  ASSERT_NE(nullptr, ret);
}

TEST_F(HashLBTest, SetGatesVainllaFailsOOB) {
  arg = snobj_int(MAX_HLB_GATES + 1);
  ret = hl0->RunCommand("set_gates", arg);
  ::snobj_free_and_null(&arg);
  ASSERT_NE(nullptr, ret);

  arg = snobj_int(-1);
  ret = hl0->RunCommand("set_gates", arg);
  ::snobj_free_and_null(&arg);
  ASSERT_NE(nullptr, ret);
}

TEST_F(HashLBTest, SetGatesVanilla) {
  int n_gates = 16;
  arg = snobj_int(n_gates);
  ret = hl0->RunCommand("set_gates", arg);
  ::snobj_free_and_null(&arg);
  ASSERT_EQ(nullptr, ret);
  ASSERT_EQ(n_gates, hl0->num_gates_);
  for (int i = 0; i < n_gates; i++) {
    ASSERT_EQ(i, hl0->gates_[i]);
  }
}

TEST_F(HashLBTest, SetGatesListFailsOOB) {
  arg = snobj_list();
  for (int i = 0; i < MAX_HLB_GATES + 1; i++) {
    snobj_list_add(arg, snobj_int(i));
  }
  ret = hl0->RunCommand("set_gates", arg);
  ::snobj_free_and_null(&arg);
  ASSERT_NE(nullptr, ret);
}

TEST_F(HashLBTest, SetGatesList) {
  arg = snobj_list();
  int n_gates = MAX_HLB_GATES / 2;
  for (int i = 0; i < n_gates; i++) {
    snobj_list_add(arg, snobj_int(n_gates - i));
  }
  ret = hl0->RunCommand("set_gates", arg);
  ::snobj_free_and_null(&arg);

  ASSERT_EQ(nullptr, ret);
  ASSERT_EQ(n_gates, hl0->num_gates_);
  for (int i = 0; i < n_gates; i++) {
    ASSERT_EQ(n_gates - i, hl0->gates_[i]);
  }
}

TEST_F(HashLBTest, SetGatesListFailsInvalidArg) {
  arg = snobj_list();
  snobj_list_add(arg, snobj_str("banana"));
  ret = hl0->RunCommand("set_gates", arg);
  ::snobj_free_and_null(&arg);
  ASSERT_NE(nullptr, ret);
  snobj_free_and_null(&ret);

  arg = snobj_list();
  snobj_list_add(arg, snobj_int(MAX_HLB_GATES + 1));
  ret = hl0->RunCommand("set_gates", arg);
  ::snobj_free_and_null(&arg);
  ASSERT_NE(nullptr, ret);
}

TEST_F(HashLBTest, InitNoArgs) {
  arg = snobj_nil();
  ret = hl0->Init(arg);
  ::snobj_free_and_null(&arg);
  ASSERT_NE(nullptr, ret);
  ::snobj_free_and_null(&ret);

  ASSERT_FALSE(Init(-1, nullptr));
}

TEST_F(HashLBTest, Init) {
  int n_gates = 16;
  ASSERT_TRUE(Init(n_gates, nullptr));
  for (int i = 0; i < n_gates; i++) {
    ASSERT_EQ(i, hl0->gates_[i]);
  }
}

TEST_F(HashLBTest, InitWithMode) {
  ASSERT_TRUE(Init(16, snobj_str("l3")));
  ASSERT_EQ(LB_L3, hl0->mode_);
}

}  // namespace modules
}  // namespace bess
