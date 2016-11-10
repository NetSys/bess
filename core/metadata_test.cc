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
    default_pipeline.attributes_.clear();
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
  struct Attribute attr0_s1 = {
      .name = "attr0",
      .size = 1,
      .mode = Attribute::AccessMode::kRead,
      .scope_id = -1,
  };
  struct Attribute attr0_s2 = {
      .name = "attr0",
      .size = 2,
      .mode = Attribute::AccessMode::kWrite,
      .scope_id = -1,
  };

  ASSERT_EQ(0, default_pipeline.RegisterAttribute(&attr0_s1));
  ASSERT_EQ(-EEXIST, default_pipeline.RegisterAttribute(&attr0_s2));
}

TEST_F(MetadataTest, DisconnectedFails) {
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 1, Attribute::AccessMode::kWrite));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 1, Attribute::AccessMode::kRead));
  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());
  ASSERT_TRUE(m1->attr_offsets[0] < 0);
}

TEST_F(MetadataTest, SingleAttrSimplePipe) {
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 1, Attribute::AccessMode::kWrite));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 1, Attribute::AccessMode::kRead));
  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  // Check that m0 was assigned a valid offset
  ASSERT_TRUE(m1->attr_offsets[0] >= 0);

  // Check that m0 and m1 agree on where to read/write a
  ASSERT_EQ(m0->attr_offsets[0], m1->attr_offsets[0]);
}

// Check that the "error" offsets arre assigned correctly
TEST_F(MetadataTest, SingleAttrSimplePipeBackwardsFails) {
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 1, Attribute::AccessMode::kRead));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 1, Attribute::AccessMode::kWrite));

  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  ASSERT_EQ(kMetadataOffsetNoRead, m0->attr_offsets[0]);
  ASSERT_EQ(kMetadataOffsetNoWrite, m1->attr_offsets[0]);
}

// Check that offsets are properly assigned when there are too many attributes.
TEST_F(MetadataTest, MultipleAttrSimplePipeNoSpaceFails) {
  size_t sz = kMetadataAttrMaxSize;
  size_t n = kMetadataTotalSize / sz;
  for (size_t i = 0; i <= n ; i++) {
    std::string s = "attr" + std::to_string(i);
    ASSERT_EQ(i, m0->AddMetadataAttr(s, sz, Attribute::AccessMode::kWrite));
    ASSERT_EQ(i, m1->AddMetadataAttr(s, sz, Attribute::AccessMode::kRead));
  }
  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  ASSERT_EQ(kMetadataOffsetNoSpace, m0->attr_offsets[n]);
  ASSERT_EQ(kMetadataOffsetNoSpace, m1->attr_offsets[n]);
}

TEST_F(MetadataTest, MultipeAttrSimplePipe) {
  bool dummy_meta[kMetadataTotalSize] = {};
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 2, Attribute::AccessMode::kWrite));
  ASSERT_EQ(1, m0->AddMetadataAttr("b", 3, Attribute::AccessMode::kWrite));
  ASSERT_EQ(2, m0->AddMetadataAttr("c", 5, Attribute::AccessMode::kWrite));
  ASSERT_EQ(3, m0->AddMetadataAttr("d", 8, Attribute::AccessMode::kWrite));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 2, Attribute::AccessMode::kRead));
  ASSERT_EQ(1, m1->AddMetadataAttr("b", 3, Attribute::AccessMode::kRead));
  ASSERT_EQ(2, m1->AddMetadataAttr("c", 5, Attribute::AccessMode::kRead));
  ASSERT_EQ(3, m1->AddMetadataAttr("d", 8, Attribute::AccessMode::kRead));
  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  size_t i = 0;
  for (const auto &attr : m0->all_attrs()) {
    // Check that m1 is reading from where m0 is writing
    ASSERT_EQ(m1->attr_offsets[i], m0->attr_offsets[i]);

    if (attr.mode != Attribute::AccessMode::kRead) {
      // Check that m0 was assigned non-overlapping offsets for writes
      mt_offset_t offset = m0->attr_offsets[i];
      ASSERT_LE(0, offset);
      for (size_t j = 0; j < attr.size; j++) {
        ASSERT_FALSE(dummy_meta[offset + j]);
        dummy_meta[offset + j] = true;
      }
    }
    i++;
  }
}

TEST_F(MetadataTest, MultipeAttrComplexPipe) {
  ModuleBuilder::DestroyAllModules();
  std::vector<Module *> mods;
  for (int i = 0; i < 10; i++) {
    mods.push_back(create_foo());
  }

  mods[0]->AddMetadataAttr("foo", 2, Attribute::AccessMode::kWrite);
  mods[1]->AddMetadataAttr("bar", 2, Attribute::AccessMode::kWrite);
  mods[2]->AddMetadataAttr("foo", 2, Attribute::AccessMode::kRead);
  mods[2]->AddMetadataAttr("bar", 2, Attribute::AccessMode::kRead);
  mods[3]->AddMetadataAttr("foo", 2, Attribute::AccessMode::kWrite);
  mods[4]->AddMetadataAttr("foo", 2, Attribute::AccessMode::kRead);
  mods[5]->AddMetadataAttr("bar", 2, Attribute::AccessMode::kWrite);
  mods[6]->AddMetadataAttr("bar", 2, Attribute::AccessMode::kRead);
  mods[6]->AddMetadataAttr("foo", 2, Attribute::AccessMode::kWrite);
  mods[7]->AddMetadataAttr("bar", 2, Attribute::AccessMode::kWrite);
  mods[8]->AddMetadataAttr("foo", 2, Attribute::AccessMode::kWrite);
  mods[9]->AddMetadataAttr("foo", 2, Attribute::AccessMode::kRead);
  mods[9]->AddMetadataAttr("bar", 2, Attribute::AccessMode::kRead);

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
  for (const auto &m : mods) {
    bool dummy_meta[kMetadataTotalSize] = {};
    size_t i = 0;
    for (const auto &attr : m->all_attrs()) {
      if (attr.mode != Attribute::AccessMode::kRead) {
        mt_offset_t offset = m->attr_offsets[i];
        if (offset < 0) {
          EXPECT_EQ(m, mods[6]);
          EXPECT_EQ(1, i);
          EXPECT_STREQ("foo", attr.name.c_str());
        } else {
          for (size_t j = 0; j < attr.size; j++) {
            ASSERT_FALSE(dummy_meta[offset + j]);
            dummy_meta[offset + j] = true;
          }
        }
      }
      i++;
    }
  }

  // This write is never read by anyone
  ASSERT_EQ(kMetadataOffsetNoWrite, mods[6]->attr_offsets[1]);

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

}  // namespace metadata
}  // namespace bess
