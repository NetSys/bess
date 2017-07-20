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
