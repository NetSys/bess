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

#include "cuckoo_map.h"

#include <gtest/gtest.h>

#include "random.h"

namespace {

using bess::utils::CuckooMap;

// Test Insert function
TEST(CuckooMapTest, Insert) {
  CuckooMap<uint32_t, uint16_t> cuckoo;
  EXPECT_EQ(cuckoo.Insert(1, 99)->second, 99);
  EXPECT_EQ(cuckoo.Insert(2, 98)->second, 98);
  EXPECT_EQ(cuckoo.Insert(1, 1)->second, 1);
}

// Test Find function
TEST(CuckooMapTest, Find) {
  CuckooMap<uint32_t, uint16_t> cuckoo;

  cuckoo.Insert(1, 99);
  cuckoo.Insert(2, 99);

  EXPECT_EQ(cuckoo.Find(1)->second, 99);
  EXPECT_EQ(cuckoo.Find(2)->second, 99);

  cuckoo.Insert(1, 2);
  EXPECT_EQ(cuckoo.Find(1)->second, 2);

  EXPECT_EQ(cuckoo.Find(3), nullptr);
  EXPECT_EQ(cuckoo.Find(4), nullptr);
}

// Test Remove function
TEST(CuckooMapTest, Remove) {
  CuckooMap<uint32_t, uint16_t> cuckoo;

  cuckoo.Insert(1, 99);
  cuckoo.Insert(2, 99);

  EXPECT_EQ(cuckoo.Find(1)->second, 99);
  EXPECT_EQ(cuckoo.Find(2)->second, 99);

  EXPECT_TRUE(cuckoo.Remove(1));
  EXPECT_TRUE(cuckoo.Remove(2));

  EXPECT_EQ(cuckoo.Find(1), nullptr);
  EXPECT_EQ(cuckoo.Find(2), nullptr);
}

// Test Count function
TEST(CuckooMapTest, Count) {
  CuckooMap<uint32_t, uint16_t> cuckoo;

  EXPECT_EQ(cuckoo.Count(), 0);

  cuckoo.Insert(1, 99);
  cuckoo.Insert(2, 99);
  EXPECT_EQ(cuckoo.Count(), 2);

  cuckoo.Insert(1, 2);
  EXPECT_EQ(cuckoo.Count(), 2);

  EXPECT_TRUE(cuckoo.Remove(1));
  EXPECT_TRUE(cuckoo.Remove(2));
  EXPECT_EQ(cuckoo.Count(), 0);
}

// Test Clear function
TEST(CuckooMapTest, Clear) {
  CuckooMap<uint32_t, uint16_t> cuckoo;

  EXPECT_EQ(cuckoo.Count(), 0);

  cuckoo.Insert(1, 99);
  cuckoo.Insert(2, 99);
  EXPECT_EQ(cuckoo.Count(), 2);

  cuckoo.Clear();
  EXPECT_EQ(cuckoo.Count(), 0);

  EXPECT_FALSE(cuckoo.Remove(1));
  EXPECT_FALSE(cuckoo.Remove(2));
}

// Test iterators
TEST(CuckooMapTest, Iterator) {
  CuckooMap<uint32_t, uint16_t> cuckoo;

  EXPECT_EQ(cuckoo.begin(), cuckoo.end());

  cuckoo.Insert(1, 99);
  cuckoo.Insert(2, 99);
  auto it = cuckoo.begin();
  EXPECT_EQ(it->first, 1);
  EXPECT_EQ(it->second, 99);

  ++it;
  EXPECT_EQ(it->first, 2);
  EXPECT_EQ(it->second, 99);

  it++;
  EXPECT_EQ(it, cuckoo.end());
}

// Test different keys with the same hash value
TEST(CuckooMapTest, CollisionTest) {
  class BrokenHash {
   public:
    bess::utils::HashResult operator()(const uint32_t) const {
      return 9999999;
    }
  };

  CuckooMap<int, int, BrokenHash> cuckoo;

  // Up to 8 (2 * slots/bucket) hash collision should be acceptable
  const int n = 8;
  for (int i = 0; i < n; i++) {
    EXPECT_TRUE(cuckoo.Insert(i, i + 100));
  }
  EXPECT_EQ(nullptr, cuckoo.Insert(n, n + 100));

  for (int i = 0; i < n; i++) {
    auto *ret = cuckoo.Find(i);
    CHECK_NOTNULL(ret);
    EXPECT_EQ(i + 100, ret->second);
  }
}

// RandomTest
TEST(CuckooMapTest, RandomTest) {
  typedef uint32_t key_t;
  typedef uint64_t value_t;

  const size_t iterations = 10000000;
  const size_t array_size = 100000;
  value_t truth[array_size] = {0};  // 0 means empty
  Random rd;

  CuckooMap<key_t, value_t> cuckoo;

  // populate with 50% occupancy
  for (size_t i = 0; i < array_size / 2; i++) {
    key_t idx = rd.GetRange(array_size);
    value_t val = static_cast<value_t>(rd.Get()) + 1;
    truth[idx] = val;
    cuckoo.Insert(idx, val);
  }

  // check if the initial population succeeded
  for (size_t i = 0; i < array_size; i++) {
    auto ret = cuckoo.Find(i);
    //std::cout << i << ' ' << idx << ' ' << truth[idx] << std::endl;
    if (truth[i] == 0) {
      EXPECT_EQ(nullptr, ret);
    } else {
      CHECK_NOTNULL(ret);
      EXPECT_EQ(truth[i], ret->second);
    }
  }

  for (size_t i = 0; i < iterations; i++) {
    uint32_t odd = rd.GetRange(10);
    key_t idx = rd.GetRange(array_size);

    if (odd == 0) {
      // 10% insert
      value_t val = static_cast<value_t>(rd.Get()) + 1;
      auto ret = cuckoo.Insert(idx, val);
      EXPECT_NE(nullptr, ret);
      truth[idx] = val;
    } else if (odd == 1) {
      // 10% delete
      bool ret = cuckoo.Remove(idx);
      EXPECT_EQ(truth[idx] != 0, ret);
      truth[idx] = 0;
    } else {
      // 80% lookup
      auto ret = cuckoo.Find(idx);
      //std::cout << i << ' ' << idx << ' ' << truth[idx] << std::endl;
      if (truth[idx] == 0) {
        EXPECT_EQ(nullptr, ret);
      } else {
        CHECK_NOTNULL(ret);
        EXPECT_EQ(truth[idx], ret->second);
      }
    }
  }
}

}  // namespace (unnamed)
