// Copyright (c) 2014-2016, The Regents of the University of California.
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

#include "metadata.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

#include "module.h"

namespace {

class Foo : public Module {
 public:
  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;
};

const Commands Foo::cmds = {};

Module *create_foo(const std::string name = "") {
  const ModuleBuilder &builder =
      ModuleBuilder::all_module_builders().find("Foo")->second;

  Module *m;
  if (name.size() == 0) {
    const std::string &mod_name = ModuleBuilder::GenerateDefaultName(
        builder.class_name(), builder.name_template());

    m = builder.CreateModule(mod_name, &bess::metadata::default_pipeline);
  } else {
    m = builder.CreateModule(std::string(name),
                             &bess::metadata::default_pipeline);
  }
  ModuleBuilder::AddModule(m);

  return m;
}

DEF_MODULE(Foo, "foo", "bip");

}  // namespace

namespace bess {
namespace metadata {

class MetadataTest : public ::testing::Test {
 protected:
  MetadataTest() : m0(), m1(), Foo_singleton() {}

  virtual void SetUp() {
    default_pipeline.CleanupMetadataComputation();
    default_pipeline.registered_attrs_.clear();
    m0 = ::create_foo();
    m1 = ::create_foo();
    ASSERT_TRUE(m0);
    ASSERT_TRUE(m1);
  }

  virtual void TearDown() { ModuleBuilder::DestroyAllModules(); }

  Module *m0;
  Module *m1;
  Foo_class Foo_singleton;

 private:
  DISALLOW_COPY_AND_ASSIGN(MetadataTest);
};

TEST(Metadata, RegisterSizeMismatchFails) {
  ASSERT_EQ(0, default_pipeline.RegisterAttribute("attr0", 1));
  ASSERT_EQ(-EINVAL, default_pipeline.RegisterAttribute("attr0", 2));

  default_pipeline.DeregisterAttribute("attr0");
  ASSERT_EQ(0, default_pipeline.RegisterAttribute("attr0", 2));

  default_pipeline.DeregisterAttribute("attr0");
}

TEST(Metadata, RegisterCount) {
  ASSERT_EQ(0, default_pipeline.RegisterAttribute("a", 4));
  ASSERT_EQ(0, default_pipeline.RegisterAttribute("a", 4));
  ASSERT_EQ(0, default_pipeline.RegisterAttribute("a", 4));
  // here the count should be 3

  ASSERT_EQ(-EINVAL, default_pipeline.RegisterAttribute("a", 8));
  default_pipeline.DeregisterAttribute("a");

  ASSERT_EQ(-EINVAL, default_pipeline.RegisterAttribute("a", 8));
  default_pipeline.DeregisterAttribute("a");

  ASSERT_EQ(-EINVAL, default_pipeline.RegisterAttribute("a", 8));
  default_pipeline.DeregisterAttribute("a");

  // now the count should be 0
  ASSERT_EQ(0, default_pipeline.RegisterAttribute("a", 8));
  default_pipeline.DeregisterAttribute("a");
}

TEST_F(MetadataTest, DisconnectedFails) {
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 1, Attribute::AccessMode::kWrite));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 1, Attribute::AccessMode::kRead));
  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());
  ASSERT_LT(m1->attr_offset(0), 0);
}

TEST_F(MetadataTest, SingleAttrSimplePipe) {
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 1, Attribute::AccessMode::kWrite));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 1, Attribute::AccessMode::kRead));
  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  // Check that m0 was assigned a valid offset
  ASSERT_GE(m1->attr_offset(0), 0);

  // Check that m0 and m1 agree on where to read/write a
  ASSERT_EQ(m0->attr_offset(0), m1->attr_offset(0));
}

// Check that the "error" offsets arre assigned correctly
TEST_F(MetadataTest, SingleAttrSimplePipeBackwardsFails) {
  ASSERT_EQ(0, m0->AddMetadataAttr("a", 1, Attribute::AccessMode::kRead));
  ASSERT_EQ(0, m1->AddMetadataAttr("a", 1, Attribute::AccessMode::kWrite));

  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  ASSERT_EQ(kMetadataOffsetNoRead, m0->attr_offset(0));
  ASSERT_EQ(kMetadataOffsetNoWrite, m1->attr_offset(0));
}

// Check that offsets are properly assigned when there are too many attributes.
TEST_F(MetadataTest, MultipleAttrSimplePipeNoSpaceFails) {
  size_t sz = kMetadataAttrMaxSize;
  size_t n = kMetadataTotalSize / sz;
  for (size_t i = 0; i <= n; i++) {
    std::string s = "attr" + std::to_string(i);
    ASSERT_EQ(i, m0->AddMetadataAttr(s, sz, Attribute::AccessMode::kWrite));
    ASSERT_EQ(i, m1->AddMetadataAttr(s, sz, Attribute::AccessMode::kRead));
  }
  m0->ConnectModules(0, m1, 0);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  ASSERT_EQ(kMetadataOffsetNoSpace, m0->attr_offset(n));
  ASSERT_EQ(kMetadataOffsetNoSpace, m1->attr_offset(n));
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
    ASSERT_EQ(m1->attr_offset(i), m0->attr_offset(i));

    if (attr.mode != Attribute::AccessMode::kRead) {
      // Check that m0 was assigned non-overlapping offsets for writes
      mt_offset_t offset = m0->attr_offset(i);
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
        mt_offset_t offset = m->attr_offset(i);
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
  ASSERT_EQ(kMetadataOffsetNoWrite, mods[6]->attr_offset(1));

  // Check that those assignments conform to the way the modules are connected
  ASSERT_NE(mods[0]->attr_offset(0), mods[1]->attr_offset(0));
  ASSERT_EQ(mods[0]->attr_offset(0), mods[2]->attr_offset(0));
  ASSERT_NE(mods[1]->attr_offset(0), mods[4]->attr_offset(0));
  ASSERT_EQ(mods[0]->attr_offset(0), mods[4]->attr_offset(0));
  ASSERT_EQ(mods[3]->attr_offset(0), mods[4]->attr_offset(0));
  ASSERT_EQ(mods[5]->attr_offset(0), mods[6]->attr_offset(0));
  ASSERT_NE(mods[5]->attr_offset(0), mods[6]->attr_offset(1));
  ASSERT_EQ(mods[7]->attr_offset(0), mods[6]->attr_offset(0));
  ASSERT_NE(mods[7]->attr_offset(0), mods[6]->attr_offset(1));
  ASSERT_NE(mods[7]->attr_offset(0), mods[8]->attr_offset(0));
  ASSERT_EQ(mods[7]->attr_offset(0), mods[9]->attr_offset(1));
  ASSERT_EQ(mods[8]->attr_offset(0), mods[9]->attr_offset(0));
}

// In this strange edge case, m4 should not clobber m3's write of attribute "h".
// We force a strange ordering of lexographic ordering of modules so to yield a
// non-monotonic ordering of the degrees of the scope componenets corresponding
// to each metadata attribute. ComputeMetadataOffsets() should sort them before
// handing them to AssignOffsets(). If it doesn't, bad things happen.
TEST_F(MetadataTest, ScopeComponentDegreeOrder) {
  ModuleBuilder::DestroyAllModules();
  m0 = create_foo("foo5");
  m1 = create_foo("foo3");
  Module *m2 = create_foo("foo6");
  Module *m3 = create_foo("foo2");
  Module *m4 = create_foo("foo4");
  Module *m5 = create_foo("foo1");
  ASSERT_NE(nullptr, m0);
  ASSERT_NE(nullptr, m1);
  ASSERT_NE(nullptr, m2);
  ASSERT_NE(nullptr, m3);
  ASSERT_NE(nullptr, m4);
  ASSERT_NE(nullptr, m5);

  m0->AddMetadataAttr("a", 4, Attribute::AccessMode::kWrite);
  m0->AddMetadataAttr("b", 4, Attribute::AccessMode::kWrite);
  m0->AddMetadataAttr("c", 4, Attribute::AccessMode::kWrite);
  m0->ConnectModules(0, m1, 0);

  m1->AddMetadataAttr("a", 4, Attribute::AccessMode::kWrite);
  m1->AddMetadataAttr("b", 4, Attribute::AccessMode::kWrite);
  m1->AddMetadataAttr("c", 4, Attribute::AccessMode::kWrite);
  m1->ConnectModules(0, m2, 0);

  m2->AddMetadataAttr("a", 4, Attribute::AccessMode::kRead);
  m2->AddMetadataAttr("b", 4, Attribute::AccessMode::kRead);
  m2->AddMetadataAttr("c", 4, Attribute::AccessMode::kRead);
  m2->AddMetadataAttr("d", 4, Attribute::AccessMode::kWrite);
  m2->AddMetadataAttr("e", 4, Attribute::AccessMode::kWrite);
  m2->AddMetadataAttr("f", 1, Attribute::AccessMode::kWrite);
  m2->ConnectModules(0, m3, 0);

  m3->AddMetadataAttr("d", 4, Attribute::AccessMode::kRead);
  m3->AddMetadataAttr("e", 4, Attribute::AccessMode::kRead);
  m3->AddMetadataAttr("f", 1, Attribute::AccessMode::kRead);
  m3->AddMetadataAttr("g", 4, Attribute::AccessMode::kWrite);
  m3->AddMetadataAttr("h", 2, Attribute::AccessMode::kWrite);
  m3->ConnectModules(0, m4, 0);

  m4->AddMetadataAttr("i", 6, Attribute::AccessMode::kWrite);
  m4->AddMetadataAttr("j", 6, Attribute::AccessMode::kWrite);
  m4->ConnectModules(0, m5, 0);

  m5->AddMetadataAttr("i", 6, Attribute::AccessMode::kRead);
  m5->AddMetadataAttr("j", 6, Attribute::AccessMode::kRead);
  m5->AddMetadataAttr("h", 2, Attribute::AccessMode::kRead);

  ASSERT_EQ(0, default_pipeline.ComputeMetadataOffsets());

  ASSERT_TRUE((m4->attr_offset(0) >= m3->attr_offset(4) + 2) ||
              (m4->attr_offset(0) + 6 <= m3->attr_offset(4)));

  ASSERT_TRUE((m4->attr_offset(1) >= m3->attr_offset(4) + 2) ||
              (m4->attr_offset(1) + 6 <= m3->attr_offset(4)));
}

}  // namespace metadata
}  // namespace bess
