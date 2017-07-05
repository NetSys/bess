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

#include "codel.h"

#include <cmath>
#include <ctime>

#include <gtest/gtest.h>

namespace {

using bess::utils::Codel;
using bess::utils::Queue;
void integer_drop(int* ptr) {
  delete ptr;
}

// Simple test to make sure one can get back out object
TEST(CodelTest, SingleInputOutput) {
  Codel<int*> c(&integer_drop, 8, 5000000, 100000000);
  int* val = new int();
  ASSERT_NE(val, nullptr);
  ASSERT_FALSE(c.Push(val));
  ASSERT_EQ(c.Size(), 1);
  int* output;
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, val);
  ASSERT_EQ(c.Size(), 0);
  EXPECT_EQ(output, val);
  delete val;
}

// Simple test to make sure one can get back out non-pointer object
TEST(CodelTest, NonPointerType) {
  Codel<int> c(NULL, 8, 5000000, 100000000);
  int val = 3439148;
  ASSERT_FALSE(c.Push(val));
  ASSERT_EQ(c.Size(), 1);
  int output;
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, val);
  ASSERT_EQ(c.Size(), 0);
  EXPECT_EQ(output, val);
}

// Tests to make sure that low delay entries won't be dropped
TEST(CodelTest, NoDropTest) {
  Codel<int*> c(&integer_drop, 8, 5000000, 100000000);

  int n = 4;
  int* vals[n];
  for (int i = 0; i < n; i++) {
    vals[i] = new int();
  }
  for (int i = 0; i < n; i++) {
    ASSERT_FALSE(c.Push(vals[i]));
  }
  EXPECT_EQ(c.Size(), n);

  usleep(3000);  // low delay should not cause any drops
  int* output = nullptr;
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[0]);

  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[1]);

  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[2]);

  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[3]);
  EXPECT_EQ(c.Size(), 0);

  for (int i = 0; i < n; i++) {
    delete vals[i];
  }
}

// Tests to make sure that codel enters drop state at the proper time and
// properly drops at the correct time.
TEST(CodelTest, DropTest) {
  Codel<int*> c(&integer_drop, 16, 5000000, 100000000);

  int n = 10;
  int* vals[n];
  for (int i = 0; i < n; i++) {
    vals[i] = new int();
  }
  for (int i = 0; i < n; i++) {
    ASSERT_FALSE(c.Push(vals[i]));
  }
  EXPECT_EQ(c.Size(), n);

  usleep(10000);  // all objects will have greater delay than target of 5ms
  int* output = nullptr;

  // should mark above target after dequeuing this entry
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[0]);

  // still shouldn't drop yet because hasn't been interval since
  // being above target
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[1]);
  usleep(105000);  // wait above a window of time to make next entry drop

  // Should be above target for window.
  // should drop vals[2], enter drop state, and return 4th instead.
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[3]);

  // should now in be drop state and the next drop should be 100ms.
  usleep(72000);
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[4]);  // shouldn't be a drop yet only has been 72ms.

  usleep(30000);
  ASSERT_FALSE(c.Pop(output));
  // vals[5] should be dropped now has been over 100ms.
  ASSERT_EQ(output, vals[6]);
  // the next drop should be in approximately 70ms(100/sqrt(2))

  usleep(40000);
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[7]);  // it has only been 40ms should not be a drop yet

  usleep(34000);
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[9]);  // vals[8] should have been dropped

  // codel queue should be empty and return nullptr
  ASSERT_TRUE(c.Pop(output));
  EXPECT_EQ(c.Size(), 0);

  delete vals[0];
  delete vals[1];
  delete vals[3];
  delete vals[4];
  delete vals[6];
  delete vals[7];
  delete vals[9];
}

// Makes sure that codel will exit drop state at the proper time both when the
// front entry is low delay and when the object that returns after dropping the
// front entry is low delay.
TEST(CodelTest, ChangeStateTest) {
  Codel<int*> c(&integer_drop, 16, 5000000, 100000000);
  int n = 10;
  int* vals[n];
  for (int i = 0; i < n; i++) {
    vals[i] = new int();
  }

  for (int i = 0; i < 4; i++) {
    ASSERT_FALSE(c.Push(vals[i]));
  }
  EXPECT_EQ(c.Size(), 4);

  usleep(10000);  // all packets will have greater delay than target of 5ms
  int* output;

  // should change into mark above target after dequeuing this entry
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[0]);

  usleep(105000);  // wait above a window of time to make next entry drop

  // Should be above target for window.
  // should drop vals[1], enter drop state, and return 3rd instead.
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[2]);

  // should now in be drop state and the next drop should be 100ms.
  usleep(104000);
  ASSERT_FALSE(c.Push(vals[4]));
  ASSERT_FALSE(c.Pop(output));
  // vals[3] should be dropped now has been over 100ms.
  ASSERT_EQ(output, vals[4]);
  // val4 had a delay below target so should have left drop state.

  for (int i = 5; i < 8; i++) {
    ASSERT_FALSE(c.Push(vals[i]));
  }
  usleep(72000);  // if we didn't exit this would result in next drop
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[5]);  // should now be marked as above target

  usleep(205000);  // wait above two windows of time to make next entry drop
  // Should be above target for window.
  // should drop vals[6], enter drop state, and return 8th instead.
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[7]);

  ASSERT_FALSE(c.Push(vals[8]));
  ASSERT_FALSE(c.Push(vals[9]));
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[8]);
  // should have exited drop state as delay below target

  usleep(72000);  // if we didn't exit this would result in next drop
  ASSERT_FALSE(c.Pop(output));
  ASSERT_EQ(output, vals[9]);

  // codel queue should be empty and return nullptr
  ASSERT_TRUE(c.Pop(output));
  EXPECT_EQ(c.Size(), 0);

  delete vals[0];
  delete vals[2];
  delete vals[4];
  delete vals[5];
  delete vals[7];
  delete vals[8];
  delete vals[9];
}

// simple test to make sure that multiple objects can be enqueued and dequeued
// at the same time
TEST(CodelTest, MultiPushPop) {
  Codel<int*> c(&integer_drop, 16, 5000000, 100000000);
  int n = 6;
  int* vals[n];
  for (int i = 0; i < n; i++) {
    vals[i] = new int();
  }
  ASSERT_EQ(c.Push(vals, n), n);
  EXPECT_EQ(c.Size(), n);

  int** output = new int*[n];
  ASSERT_EQ(c.Pop(output, n), n);
  for (int i = 0; i < n; i++) {
    ASSERT_EQ(output[i], vals[i]);
  }

  for (int i = 0; i < n; i++) {
    delete vals[i];
  }
  delete[] output;
}

}  // namespace
