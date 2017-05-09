#include "extended_priority_queue.h"

#include <gtest/gtest.h>

#include "random.h"

using bess::utils::extended_priority_queue;

namespace {

// Tests decrease_key_top()
TEST(ExtendedPriorityQueueTest, DecreaseKeyTop) {
  extended_priority_queue<int> queue;
  queue.push(1);
  queue.push(10000);
  queue.push(100);
  queue.push(1000);
  EXPECT_TRUE(std::is_heap(queue.container().begin(), queue.container().end()));

  int& top = queue.mutable_top();
  EXPECT_EQ(top, 10000);

  top = 10;
  EXPECT_EQ(queue.top(), 10);

  queue.decrease_key_top();
  EXPECT_EQ(queue.top(), 1000);
}

// Tests delete_single_element()
TEST(ExtendedPriorityQueueTest, Delete) {
  extended_priority_queue<int> queue;
  queue.push(1);
  queue.push(10000);
  queue.push(100);
  queue.push(1000);

  EXPECT_EQ(queue.size(), 4);
  EXPECT_EQ(queue.top(), 10000);

  EXPECT_FALSE(
      queue.delete_single_element([=](const int& x) { return x == 10; }));

  EXPECT_EQ(queue.size(), 4);
  EXPECT_EQ(queue.top(), 10000);

  EXPECT_TRUE(
      queue.delete_single_element([=](const int& x) { return x == 1; }));
  EXPECT_EQ(queue.size(), 3);
  EXPECT_EQ(queue.top(), 10000);

  EXPECT_TRUE(
      queue.delete_single_element([=](const int& x) { return x == 10000; }));
  EXPECT_EQ(queue.size(), 2);
  EXPECT_EQ(queue.top(), 1000);
}

}  // namespace (unnamed)
