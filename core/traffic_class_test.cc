// Unit tests traffic class and scheduler routines.

#include "scheduler.h"
#include "traffic_class.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#define TC TrafficClassBuilder::CreateTree

namespace bess {

using namespace traffic_class_initializer_types;

TEST(CreateTree, PriorityRootAndLeaf) {
  TrafficClass *tree = TC("root", {PRIORITY}, {{PRIORITY, 10, TC("leaf", {LEAF})}});

  ASSERT_TRUE(tree != nullptr);
  EXPECT_EQ(POLICY_PRIORITY, tree->policy());

  PriorityTrafficClass *pc = static_cast<PriorityTrafficClass *>(tree);
  ASSERT_TRUE(pc != nullptr);
  ASSERT_EQ(1, pc->children().size());
  EXPECT_EQ(10, pc->children()[0].priority_);

  LeafTrafficClass *leaf = static_cast<LeafTrafficClass *>(pc->children()[0].c_);
  ASSERT_TRUE(leaf != nullptr);
  EXPECT_EQ(leaf->parent(), pc);
}


}  // namespace bess
