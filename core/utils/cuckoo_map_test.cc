#include "cuckoo_map.h"

#include <rte_config.h>
#include <rte_hash.h>
#include <rte_hash_crc.h>

#include <gtest/gtest.h>

using bess::utils::CuckooMap;

namespace {

typedef uint16_t value_t;

// Test Insert function
TEST(CuckooMapTest, Insert) {
  CuckooMap<uint32_t, value_t> cuckoo;
  EXPECT_EQ(cuckoo.Insert(1, 99)->second, 99);
  EXPECT_EQ(cuckoo.Insert(2, 98)->second, 98);
  EXPECT_EQ(cuckoo.Insert(1, 1)->second, 1);
}

// Test Find function
TEST(CuckooMapTest, Find) {
  CuckooMap<uint32_t, value_t> cuckoo;

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
  CuckooMap<uint32_t, value_t> cuckoo;

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
  CuckooMap<uint32_t, value_t> cuckoo;

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
  CuckooMap<uint32_t, value_t> cuckoo;

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
  CuckooMap<uint32_t, value_t> cuckoo;

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

}  // namespace (unnamed)
