// Copyright (c) 2017, Joshua Stone.
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

#include "lock_less_queue.h"

#include <gtest/gtest.h>

namespace {

using bess::utils::LockLessQueue;
using bess::utils::Queue;

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

// simple test to make sure that the queue is resized properly
TEST(LLQueueTest, Resize) {
  LockLessQueue<int*> q(8);

  int n = 6;
  int* vals1[n];
  int* vals2[n];
  for (int i = 0; i < n; i++) {
    vals1[i] = new int();
    vals2[i] = new int();
  }
  ASSERT_EQ(q.Push(vals1, n), n);
  EXPECT_EQ(q.Size(), n);

  ASSERT_EQ(q.Resize(16), 0);
  ASSERT_EQ(q.Capacity(), 16);
  ASSERT_EQ(q.Push(vals2, n), n);
  ASSERT_EQ(q.Size(), 2 * n);

  int** output = new int*[2*n];
  ASSERT_EQ(q.Pop(output, 2 * n), 2*n);
  for (int i = 0; i < n; i++) {
    ASSERT_EQ(output[i], vals1[i]);
    ASSERT_EQ(output[i + n], vals2[i]);
  }

  for (int i = 0; i < n; i++) {
    delete vals1[i];
    delete vals2[i];
  }
  delete[] output;
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
  ASSERT_EQ(q.Pop(output, n), n);
  for (int i = 0; i < n; i++) {
    ASSERT_EQ(output[i], vals[i]);
  }

  for (int i = 0; i < n; i++) {
    delete vals[i];
  }
  delete[] output;
}

}  // namespace
