#include "lock_less_queue.h"

#include <gtest/gtest.h>

namespace {

using bess::utils::LockLessQueue;

// Simple test to make sure one can get back out object
TEST(LLQueueTest, SingleInputOutput) {
  LockLessQueue<int*> q(8);
  int* val = new int();
  ASSERT_NE(val, nullptr);
  ASSERT_FALSE(q.Push(val));
  ASSERT_EQ(q.Size(), 1);
  int* output;
  ASSERT_FALSE(q.Pop(output));
  ASSERT_EQ(output, val);
  ASSERT_EQ(q.Size(), 0);
  EXPECT_EQ(output, val);
  delete val;
}

// Tests to make sure that one can get back out multiple objects
TEST(LLQueueTest, MultiInputOutput) {
  LockLessQueue<int*> q(8);
  int n = 4;
  int* vals[n];
  for (int i = 0; i < n; i++) {
    vals[i] = new int();
  }
  for (int i = 0; i < n; i++) {
    ASSERT_FALSE(q.Push(vals[i]));
  }
  EXPECT_EQ(q.Size(), n);

  int* output = nullptr; 
  ASSERT_FALSE(q.Pop(output));
  ASSERT_EQ(output, vals[0]);
  
  ASSERT_FALSE(q.Pop(output));
  ASSERT_EQ(output, vals[1]);
  
  ASSERT_FALSE(q.Pop(output));
  ASSERT_EQ(output, vals[2]);
  
  ASSERT_FALSE(q.Pop(output));
  ASSERT_EQ(output, vals[3]);
  EXPECT_EQ(q.Size(), 0);

  for (int i = 0; i < n; i++) {
    delete vals[i];
  }
}

// simple test to make sure that multiple objects can be enqueued and dequeued
// at the same time
TEST(LLQueueTest, MultiPushPop) {
  LockLessQueue<int*> q(16);
  int n = 6;
  int* vals[n];
  for (int i = 0; i < n; i++) {
    vals[i] = new int();
  }
  ASSERT_EQ(q.Push(vals, n), n);
  EXPECT_EQ(q.Size(), n);

  int** output = new int*[n]; 
  q.Pop(output, n);
  for (int i = 0; i < n; i++) {
    ASSERT_EQ(output[i], vals[i]);
  }

  for (int i = 0; i < n; i++) {
    delete vals[i];
  }
  delete[] output;
}

}  // namespace
