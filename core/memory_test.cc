#include "memory.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iostream>

#include "utils/random.h"
#include "utils/time.h"

namespace bess {

TEST(PhyMemTest, Phy2Virt) {
  if (geteuid() == 0) {
    int x = 0;  // &x is a valid address
    if (Virt2PhyGeneric(&x) == 0) {
      std::cerr << "CAP_SYS_ADMIN capability not available."
                << " Skipping test..." << std::endl;
      return;
    }
  }
}

TEST(HugepageTest, NullFree) {
  FreeHugepage(nullptr);
}

TEST(HugepageTest, BadSize) {
  // Try 8MB hugepages, which does not exist on neither x86_64 nor i686
  ASSERT_DEATH(AllocHugepage(static_cast<HugepageSize>(1 << 23)), "");
}

class HugepageTest : public ::testing::TestWithParam<HugepageSize> {
 public:
  virtual void SetUp() override {
    HugepageSize type = GetParam();
    size_ = static_cast<size_t>(type);
    ptr_ = AllocHugepage(type);

    if (ptr_ == nullptr) {
      std::cerr << "Hugepage (" << size_ << " bytes) not available."
                << " Skipping test..." << std::endl;
    } else {
      ASSERT_LE(kVirtualAddressStart, reinterpret_cast<uintptr_t>(ptr_));
      ASSERT_GT(kVirtualAddressEnd, reinterpret_cast<uintptr_t>(ptr_));
    }
  }

  virtual void TearDown() override { FreeHugepage(ptr_); }

 protected:
  static const int kTestIterations = 100000;

  void *ptr_;
  size_t size_;  // total size: page size * number of pages
  Random rd_;
};

TEST_P(HugepageTest, BasicAlloc) {
  if (ptr_ == nullptr) {
    // The machine may not be configured with hugepages. Just skip if failed.
    return;
  }

  EXPECT_EQ(ptr_, Phy2Virt(Virt2Phy(ptr_)));

  if (geteuid() == 0) {
    EXPECT_EQ(Virt2Phy(ptr_), Virt2PhyGeneric(ptr_));
    EXPECT_EQ(ptr_, Phy2Virt(Virt2PhyGeneric(ptr_)));
  }
}

TEST_P(HugepageTest, Access) {
  if (ptr_ == nullptr) {
    // The machine may not be configured with hugepages. Just skip if failed.
    return;
  }

  uint64_t *ptr = reinterpret_cast<uint64_t *>(ptr_);
  size_t num_elems = size_ / sizeof(*ptr);

  // WRITE
  for (size_t i = 0; i < num_elems; i++) {
    ptr[i] = i + 123456789;
  }

  // READ
  for (size_t i = 0; i < num_elems; i++) {
    ASSERT_EQ(ptr[i], i + 123456789);
  }
}

TEST_P(HugepageTest, AllZero) {
  if (ptr_ == nullptr) {
    // The machine may not be configured with hugepages. Just skip if failed.
    return;
  }

  uint64_t *ptr = reinterpret_cast<uint64_t *>(ptr_);
  size_t num_elems = size_ / sizeof(*ptr);

  for (size_t i = 0; i < num_elems; i++) {
    ASSERT_EQ(ptr[i], 0);
  }
}

// The allocated page is physically contiguous?
TEST_P(HugepageTest, Contiguous) {
  if (geteuid() != 0 || ptr_ == nullptr) {
    // The machine may not be configured with hugepages. Just skip if failed.
    return;
  }

  char *ptr = reinterpret_cast<char *>(ptr_);

  for (auto i = 0; i < kTestIterations; i++) {
    size_t offset = rd_.GetRange(size_);
    ASSERT_EQ(Virt2Phy(ptr) + offset, Virt2PhyGeneric(ptr + offset))
        << "offset=" << offset;
  }
}

TEST_P(HugepageTest, LeakFree) {
  if (ptr_ == nullptr) {
    // The machine may not be configured with hugepages. Just skip if failed.
    return;
  }

  double start = get_cpu_time();

  do {
    // Already allocated, so free first
    FreeHugepage(ptr_);

    ptr_ = AllocHugepage(GetParam());
    EXPECT_NE(ptr_, nullptr);
  } while (get_cpu_time() - start < 0.5);  // 0.5 second for each page size
}

INSTANTIATE_TEST_CASE_P(PageSize, HugepageTest,
                        ::testing::Values(HugepageSize::k2MB,
                                          HugepageSize::k1GB));

TEST(DmaMemoryPoolTest, PoolSetup) {
  if (geteuid() != 0) {
    std::cerr << "CAP_SYS_ADMIN required. Skipping test..." << std::endl;
    return;
  }

  DmaMemoryPool *pool;

  ASSERT_DEATH(pool = new DmaMemoryPool(0, -1), "");
  ASSERT_DEATH(pool = new DmaMemoryPool(1024 * 1024, -2), "");

  // Assume at least 128MB of persistent hugepages are available...
  pool = new DmaMemoryPool(128 * 1024 * 1024, -1);
  if (!pool->Initialized()) {
    std::cerr << "CAP_SYS_ADMIN required. Skipping test..." << std::endl;
    return;
  }

  // Do not crash with null pointers
  pool->Free(nullptr);

  delete pool;

  // Node-specific allocation
  for (int i = 0; i < NumNumaNodes(); i++) {
    pool = new DmaMemoryPool(128 * 1024 * 1024, i);
    ASSERT_TRUE(pool->Initialized());
    delete pool;
  }
}

TEST(DmaMemoryPoolTest, Dump) {
  if (geteuid() != 0) {
    std::cerr << "CAP_SYS_ADMIN required. Skipping test..." << std::endl;
    return;
  }

  DmaMemoryPool pool(128 * 1024 * 1024, -1);
  if (!pool.Initialized()) {
    std::cerr << "CAP_SYS_ADMIN required. Skipping test..." << std::endl;
    return;
  }

  std::cout << pool.Dump();
}

TEST(DmaMemoryPoolTest, AlignedAlloc) {
  if (geteuid() != 0) {
    std::cerr << "CAP_SYS_ADMIN required. Skipping test..." << std::endl;
    return;
  }

  DmaMemoryPool pool(128 * 1024 * 1024, -1);
  if (!pool.Initialized()) {
    std::cerr << "CAP_SYS_ADMIN required. Skipping test..." << std::endl;
    return;
  }

  // should be able to alloc 128 * 1MB blocks...
  std::array<void *, 128> ptrs;

  // Try 5 full rounds, to see if cleanup was complete
  for (int k = 0; k < 5; k++) {
    for (size_t i = 0; i < ptrs.size(); i++) {
      ptrs[i] = pool.Alloc(1024 * 1024);
      ASSERT_NE(ptrs[i], static_cast<void *>(nullptr));
    }

    // then it may or may not fail (the pool may have more than 128MB)
    if (pool.TotalFreeBytes() < 1024 * 1024) {
      ASSERT_EQ(pool.Alloc(1024 * 1024), static_cast<void *>(nullptr));
    }

    for (size_t i = 0; i < ptrs.size(); i++) {
      EXPECT_EQ(Virt2Phy(ptrs[i]), Virt2PhyGeneric(ptrs[i]));
      pool.Free(ptrs[i]);
    }
  }
}

TEST(DmaMemoryPoolTest, UnalignedAlloc) {
  if (geteuid() != 0) {
    std::cerr << "CAP_SYS_ADMIN required. Skipping test..." << std::endl;
    return;
  }

  DmaMemoryPool pool(128 * 1024 * 1024, -1);
  size_t initial_free_bytes = pool.TotalFreeBytes();
  if (!pool.Initialized()) {
    std::cerr << "CAP_SYS_ADMIN required. Skipping test..." << std::endl;
    return;
  }

  std::vector<void *> ptrs;
  Random rng;

  for (int k = 0; k < 5; k++) {
    while (true) {
      size_t size_to_alloc = rng.GetRange(1024 * 1024) + 1;
      void *ptr = pool.Alloc(size_to_alloc);

      if (ptr == nullptr) {
        EXPECT_GE(ptrs.size(), 128);
        break;
      }

      ptrs.push_back(ptr);
    }

    EXPECT_LT(pool.TotalFreeBytes(), initial_free_bytes);
    std::cout << "Total free but fragmented (< 1MB) space: "
              << pool.TotalFreeBytes() << " bytes" << std::endl;

    // Shuffle
    for (size_t i = 0; i < ptrs.size() - 1; i++) {
      size_t j = i + 1 + rng.GetRange(ptrs.size() - i - 1);
      std::swap(ptrs[i], ptrs[j]);
    }

    for (void *ptr : ptrs) {
      pool.Free(ptr);
    }

    EXPECT_EQ(pool.TotalFreeBytes(), initial_free_bytes);
    ptrs.clear();
  }
}

}  // namespace bess
