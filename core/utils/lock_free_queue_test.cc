#include "lock_free_queue.h"

#include <gtest/gtest.h>

using bess::utils::LockFreeQueue;

TEST(LockFreeQueueTest, SinglePushSinglePop) {
  LockFreeQueue<int> q;
  int x = 0xDEADBEEF;
  int *y = nullptr;
  q.Push(&x);
  q.Pop(&y);
  ASSERT_EQ(y, &x);
}

TEST(LockFreeQueueTest, BulkPushSinglePop) {
  LockFreeQueue<int> q;
  int arr[2] = {0xDEAD, 0xBEEF};
  int *tbl[2] = {arr, arr + 1};
  int *y = nullptr;
  q.Push(tbl, 2);
  q.Pop(&y);
  ASSERT_EQ(y, arr);
  q.Pop(&y);
  ASSERT_EQ(y, arr + 1);
}

TEST(LockFreeQueueTest, BulkPushBulkPop) {
  LockFreeQueue<int> q;
  int arr[2] = {0xDEAD, 0xBEEF};
  int *tbl[2] = {arr, arr + 1};
  int *y[2] = {nullptr, nullptr};
  q.Push(tbl, 2);
  q.Pop(y, 2);
  ASSERT_EQ(y[0], arr);
  ASSERT_EQ(y[1], arr + 1);
}
