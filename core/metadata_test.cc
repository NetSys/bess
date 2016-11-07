#include "metadata.h"

#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

#include "module.h"

namespace {

class Foo : public Module {
 public:
  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = MAX_GATES;
  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};

const Commands<Module> Foo::cmds = {};
const PbCommands Foo::pb_cmds = {};

Module *create_foo() {
  const ModuleBuilder &builder =
      ModuleBuilder::all_module_builders().find("Foo")->second;

  const std::string &mod_name = ModuleBuilder::GenerateDefaultName(
      builder.class_name(), builder.name_template());

  Module *m = builder.CreateModule(mod_name, &bess::metadata::default_pipeline);
  builder.AddModule(m);

  return m;
}
}

namespace bess {
namespace metadata {

class MetadataTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    default_pipeline.CleanupMetadataComputation();
    ADD_MODULE(Foo, "foo", "bip")
    ASSERT_TRUE(__module__Foo);
    m0 = ::create_foo();
    m1 = ::create_foo();
    ASSERT_TRUE(m0);
    ASSERT_TRUE(m1);
  }

  virtual void TearDown() {
    ModuleBuilder::DestroyAllModules();
    ModuleBuilder::all_module_builders_holder(true);
  }

  Module *m0;
  Module *m1;
};

TEST(Metadata, RegisterSizeMismatchFails) {
  struct bess::metadata::mt_attr attr0_s1 = {
      .name = "attr0", .size = 1, .mode = bess::metadata::AccessMode::READ,
  };
  struct bess::metadata::mt_attr attr0_s2 = {
      .name = "attr0", .size = 2, .mode = bess::metadata::AccessMode::WRITE,
  };

  ASSERT_EQ(0, default_pipeline.RegisterAttribute(&attr0_s1));
  ASSERT_EQ(-EEXIST, default_pipeline.RegisterAttribute(&attr0_s2));
}

TEST_F(MetadataTest, DisconnectedFails) {
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 1, bess::metadata::AccessMode::WRITE));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 1, bess::metadata::AccessMode::READ));
  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());
  ASSERT_TRUE(m1->attr_offsets[0] < 0);
}

TEST_F(MetadataTest, SingleAttrSimplePipe) {
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 1, bess::metadata::AccessMode::WRITE));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 1, bess::metadata::AccessMode::READ));
  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  // Check that m0 was assigned a valid offset
  ASSERT_TRUE(m1->attr_offsets[0] >= 0);

  // Check that m0 and m1 agree on where to read/write a
  ASSERT_EQ(m0->attr_offsets[0], m1->attr_offsets[0]);
}

// Check that the "error" offsets arre assigned correctly
TEST_F(MetadataTest, SingleAttrSimplePipeBackwardsFails) {
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 1, bess::metadata::AccessMode::READ));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 1, bess::metadata::AccessMode::WRITE));

  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  ASSERT_EQ(bess::metadata::kMetadataOffsetNoRead, m0->attr_offsets[0]);
  ASSERT_EQ(bess::metadata::kMetadataOffsetNoWrite, m1->attr_offsets[0]);
}

// Check that offsets are properly assigned when there are too many attributes.
TEST_F(MetadataTest, MultipleAttrSimplePipeNoSpaceFails) {
  size_t sz = bess::metadata::kMetadataAttrMaxSize;
  size_t n = bess::metadata::kMetadataTotalSize / sz;
  for (size_t i = 0; i < n + 1; i++) {
    std::ostringstream os;
    os << "attr" << i;
    std::string s = os.str();
    ASSERT_EQ(i, m0->AddMetadataAttr(s, sz, bess::metadata::AccessMode::WRITE));
    ASSERT_EQ(i, m1->AddMetadataAttr(s, sz, bess::metadata::AccessMode::READ));
  }
  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  ASSERT_EQ(bess::metadata::kMetadataOffsetNoSpace, m0->attr_offsets[n]);
  ASSERT_EQ(bess::metadata::kMetadataOffsetNoSpace, m1->attr_offsets[n]);
}

TEST_F(MetadataTest, MultipeAttrSimplePipe) {
  bool dummy_meta[bess::metadata::kMetadataTotalSize] = {};
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 2, bess::metadata::AccessMode::WRITE));
  ASSERT_EQ(1, m0->AddMetadataAttr("b", 3, bess::metadata::AccessMode::WRITE));
  ASSERT_EQ(2, m0->AddMetadataAttr("c", 5, bess::metadata::AccessMode::WRITE));
  ASSERT_EQ(3, m0->AddMetadataAttr("d", 8, bess::metadata::AccessMode::WRITE));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 2, bess::metadata::AccessMode::READ));
  ASSERT_EQ(1, m1->AddMetadataAttr("b", 3, bess::metadata::AccessMode::READ));
  ASSERT_EQ(2, m1->AddMetadataAttr("c", 5, bess::metadata::AccessMode::READ));
  ASSERT_EQ(3, m1->AddMetadataAttr("d", 8, bess::metadata::AccessMode::READ));
  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  for (size_t i = 0; i < m0->num_attrs; i++) {
    // Check that m1 is reading from where m0 is writing
    ASSERT_EQ(m1->attr_offsets[i], m0->attr_offsets[i]);

    if (m0->attrs[i].mode == bess::metadata::AccessMode::READ)
      continue;

    // Check that m0 was assigned non-overlapping offsets for writes
    bess::metadata::mt_offset_t offset = m0->attr_offsets[i];
    ASSERT_TRUE(offset >= 0);
    for (mt_offset_t j = 0; j < m0->attrs[i].size; j++) {
      ASSERT_FALSE(dummy_meta[offset + j]);
      dummy_meta[offset + j] = true;
    }
  }
}

TEST_F(MetadataTest, MultipeAttrComplexPipe) {
  ModuleBuilder::DestroyAllModules();
  std::vector<Module *> mods;
  for (int i = 0; i < 10; i++) {
    mods.push_back(create_foo());
  }

  mods[0]->AddMetadataAttr("foo", 2, bess::metadata::AccessMode::WRITE);
  mods[1]->AddMetadataAttr("bar", 2, bess::metadata::AccessMode::WRITE);
  mods[2]->AddMetadataAttr("foo", 2, bess::metadata::AccessMode::READ);
  mods[2]->AddMetadataAttr("bar", 2, bess::metadata::AccessMode::READ);
  mods[3]->AddMetadataAttr("foo", 2, bess::metadata::AccessMode::WRITE);
  mods[4]->AddMetadataAttr("foo", 2, bess::metadata::AccessMode::READ);
  mods[5]->AddMetadataAttr("bar", 2, bess::metadata::AccessMode::WRITE);
  mods[6]->AddMetadataAttr("bar", 2, bess::metadata::AccessMode::READ);
  mods[6]->AddMetadataAttr("foo", 2, bess::metadata::AccessMode::WRITE);
  mods[7]->AddMetadataAttr("bar", 2, bess::metadata::AccessMode::WRITE);
  mods[8]->AddMetadataAttr("foo", 2, bess::metadata::AccessMode::WRITE);
  mods[9]->AddMetadataAttr("foo", 2, bess::metadata::AccessMode::READ);
  mods[9]->AddMetadataAttr("bar", 2, bess::metadata::AccessMode::READ);

  mods[0]->ConnectModules(0, mods[1], 0);
  mods[1]->ConnectModules(0, mods[2], 0);
  mods[1]->ConnectModules(1, mods[4], 0);
  mods[0]->ConnectModules(1, mods[4], 0);
  mods[3]->ConnectModules(0, mods[4], 0);
  mods[4]->ConnectModules(0, mods[5], 0);
  mods[5]->ConnectModules(0, mods[6], 0);
  mods[7]->ConnectModules(0, mods[6], 0);
  mods[7]->ConnectModules(1, mods[8], 0);
  mods[8]->ConnectModules(0, mods[9], 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  // Check that every module was assigned valid, non-overlapping offsets
  for (const auto &it : mods) {
    bool dummy_meta[bess::metadata::kMetadataTotalSize] = {};
    for (size_t i = 0; i < it->num_attrs; i++) {
      if (it->attrs[i].mode == bess::metadata::AccessMode::READ)
        continue;

      bess::metadata::mt_offset_t offset = it->attr_offsets[i];
      for (mt_offset_t j = 0; j < it->attrs[i].size; j++) {
        ASSERT_FALSE(dummy_meta[offset + j]);
        dummy_meta[offset + j] = true;
      }
    }
  }

  // Check that those assignments conform to the way the modules are connected
  ASSERT_NE(mods[0]->attr_offsets[0], mods[1]->attr_offsets[0]);
  ASSERT_EQ(mods[0]->attr_offsets[0], mods[2]->attr_offsets[0]);
  ASSERT_NE(mods[1]->attr_offsets[0], mods[4]->attr_offsets[0]);
  ASSERT_EQ(mods[0]->attr_offsets[0], mods[4]->attr_offsets[0]);
  ASSERT_EQ(mods[3]->attr_offsets[0], mods[4]->attr_offsets[0]);
  ASSERT_EQ(mods[5]->attr_offsets[0], mods[6]->attr_offsets[0]);
  ASSERT_NE(mods[5]->attr_offsets[0], mods[6]->attr_offsets[1]);
  ASSERT_EQ(mods[7]->attr_offsets[0], mods[6]->attr_offsets[0]);
  ASSERT_NE(mods[7]->attr_offsets[0], mods[6]->attr_offsets[1]);
  ASSERT_NE(mods[7]->attr_offsets[0], mods[8]->attr_offsets[0]);
  ASSERT_EQ(mods[7]->attr_offsets[0], mods[9]->attr_offsets[1]);
  ASSERT_EQ(mods[8]->attr_offsets[0], mods[9]->attr_offsets[0]);
}
}
}
