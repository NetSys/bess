#include "memory.h"

#include <fcntl.h>
#include <linux/mempolicy.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <syscall.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <utility>

#include "utils/common.h"
#include "utils/format.h"

namespace bess {
namespace {

// set_mempolicy() without libnuma dependency
static long linux_set_mempolicy(int mode, const unsigned long *nmask,
                                unsigned long maxnode) {
  return syscall(__NR_set_mempolicy, mode, nmask, maxnode);
}

#ifndef SHM_HUGE_SHIFT
#define SHM_HUGE_SHIFT 26
#endif

#ifndef SHM_HUGE_2MB
#define SHM_HUGE_2MB (21 << SHM_HUGE_SHIFT)
#endif

#ifndef SHM_HUGE_1GB
#define SHM_HUGE_1GB (30 << SHM_HUGE_SHIFT)
#endif

static void *DoAllocHugepage(HugepageSize page_size) {
  int shm_flags = SHM_HUGETLB | SHM_NORESERVE | IPC_CREAT | 0600;  // read/write
  size_t size = static_cast<size_t>(page_size);

  switch (page_size) {
    case HugepageSize::k2MB:
      shm_flags |= SHM_HUGE_2MB;
      break;
    case HugepageSize::k1GB:
      shm_flags |= SHM_HUGE_1GB;
      break;
    default:
      CHECK(0);
  }

  int shm_id = shmget(IPC_PRIVATE, size, shm_flags);
  if (shm_id == -1) {
    // If the hugepage size is not supported by system errno will be EINVAL.
    // This is normal, so continue (e.g., trying 4MB hugepages on x86_64)
    if (errno != EINVAL) {
      PLOG(ERROR) << "shmget() for " << size << " bytes failed";
    }
    return nullptr;
  }

  void *ptr = shmat(shm_id, nullptr, 0);
  shmctl(shm_id, IPC_RMID, 0);  // won't be freed until everyone detaches

  if (ptr == MAP_FAILED) {
    if (errno != ENOMEM) {
      PLOG(ERROR) << "shmat() for " << size << "bytes failed";
    }
    return nullptr;
  }

  if (mlock(ptr, size) != 0) {
    PLOG(ERROR) << "mlock(ptr) - check 'ulimit -l'";
    shmdt(ptr);
    return nullptr;
  }

  uintptr_t paddr = Virt2PhyGeneric(ptr);
  if (paddr == 0) {
    LOG(ERROR) << "Virt2PhyGeneric() failed";
    shmdt(ptr);
    return nullptr;
  }

  void *ptr_remapped = shmat(shm_id, Phy2Virt(paddr), 0);
  if (ptr_remapped == MAP_FAILED) {
    PLOG(ERROR) << "shmat() for remapping";
    shmdt(ptr);
    return nullptr;
  }

  // Remove the temporary mapping
  int ret = shmdt(ptr);
  PLOG_IF(ERROR, ret != 0) << "shmdt(ptr)";

  if (mlock(ptr_remapped, size) != 0) {
    PLOG(ERROR) << "mlock(ptr_remapped) - check 'ulimit -l'";
    shmdt(ptr_remapped);
    return nullptr;
  }

  return ptr_remapped;
}

}  // namespace

int NumNumaNodes() {
  static int cached = 0;
  if (cached > 0) {
    return cached;
  }

  std::ifstream fp("/sys/devices/system/node/possible");
  if (fp.is_open()) {
    std::string line;
    if (std::getline(fp, line)) {
      int cnt;
      if (utils::Parse(line, "0-%d", &cnt) == 1) {
        cached = cnt + 1;
        return cached;
      }
    }
  }

  LOG(INFO) << "/sys/devices/system/node/possible not available. "
            << "Assuming a single-node system...";
  cached = 1;
  return cached;
}

HugepageSize GetDefaultHugepageSize() {
  static HugepageSize cached = static_cast<HugepageSize>(0);
  if (static_cast<size_t>(cached) > 0) {
    return cached;
  }

  std::ifstream fp("/proc/meminfo");
  if (fp.is_open()) {
    std::string line;
    while (std::getline(fp, line)) {
      size_t size_kb;
      if (utils::Parse(line, "Hugepagesize: %zu kB", &size_kb) == 1) {
        cached = static_cast<HugepageSize>(size_kb * 1024);
        CHECK(cached == HugepageSize::k2MB || cached == HugepageSize::k1GB)
            << "Unknown hugepage size " << static_cast<size_t>(cached);
        return cached;
      }
    }
  }

  LOG(FATAL) << "Could not detect the default hugepage size from /proc/meminfo";
}

uintptr_t Virt2PhyGeneric(void *ptr) {
  const uintptr_t kPageSize = sysconf(_SC_PAGESIZE);

  uintptr_t vaddr = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t offset = vaddr % kPageSize;

  int fd = open("/proc/self/pagemap", O_RDONLY);
  if (fd < 0) {
    PLOG(ERROR) << "open(/proc/self/pagemap)";
    return 0;
  }

  uint64_t page_info;
  int ret = pread(fd, &page_info, sizeof(page_info),
                  (vaddr / kPageSize) * sizeof(page_info));
  if (ret != sizeof(page_info)) {
    PLOG(ERROR) << "pread(/proc/self/pagemap)";
  }

  close(fd);

  // See Linux Documentation/vm/pagemap.txt
  // page frame number (physical address / kPageSize) is on lower 55 bits
  uintptr_t pfn = page_info & ((1ull << 55) - 1);
  bool present = page_info & (1ull << 63);

  if (!present) {
    LOG(ERROR) << "Virt2PhyGeneric(): virtual address " << ptr
               << " is not mapped";
    return 0;
  }

  if (pfn == 0) {
    LOG_FIRST_N(ERROR, 1)
        << "Virt2PhyGeneric(): PFN for vaddr " << ptr
        << " is not available. CAP_SYS_ADMIN capability is required. "
        << "page_info = " << std::hex << page_info << std::dec;
    return 0;
  }

  uintptr_t paddr = pfn * kPageSize + offset;

  return paddr;
}

void *AllocHugepage(HugepageSize page_size) {
  if (void *ret = DoAllocHugepage(page_size)) {
    return ret;
  }

  // Reserve more hugepages and try again
  std::string dir = "/sys/kernel/mm/hugepages/hugepages-2048kB";
  size_t pages_to_add = 128;  // add 256MB at once, to minimize fragmentation

  if (page_size == HugepageSize::k1GB) {
    dir = "/sys/kernel/mm/hugepages/hugepages-1048576kB";
    pages_to_add = 1;
  }

  std::fstream fp(dir + "/nr_hugepages_mempolicy");
  if (fp) {
    size_t count;
    fp >> count;
    fp << count + pages_to_add;
    fp.close();
  } else {
    LOG(WARNING) << "cannot open " << dir << "/nr_hugepages_mempolicy";
    return nullptr;
  }

  return DoAllocHugepage(page_size);  // retry
}

void *AllocHugepageFromSocket(HugepageSize type, int socket_id) {
  // From any socket?
  if (socket_id == -1) {
    return AllocHugepage(type);
  }

  CHECK_GE(socket_id, 0);
  CHECK_LT(socket_id, NumNumaNodes());

  unsigned long mask = 1ul << socket_id;
  unsigned long maxnode = NumNumaNodes() + 1;  // number of bits in mask
  int ret;

  // Update mempolicy to allocate hugepage only from the specified node.
  ret = linux_set_mempolicy(MPOL_BIND | MPOL_F_STATIC_NODES, &mask, maxnode);
  if (ret < 0) {
    PLOG(ERROR) << "set_mempolicy(bind, " << socket_id << ")";
    return nullptr;
  }

  void *addr = AllocHugepage(type);

  // Go back to default NUMA policy
  ret = linux_set_mempolicy(MPOL_DEFAULT, nullptr, 0);
  PLOG_IF(WARNING, ret < 0) << "set_mempolicy(default)";

  return addr;
}

void FreeHugepage(void *ptr) {
  // allow null pointers
  if (!ptr) {
    return;
  }

  int ret = shmdt(ptr);
  PLOG_IF(ERROR, ret != 0) << "shmdt(ptr_remapped)";
}

DmaMemoryPool::DmaMemoryPool(size_t size, int socket_id)
    : initialized_(false), socket_id_(socket_id), total_free_bytes_(0) {
  CHECK_GT(size, 0);
  CHECK_GE(socket_id, -1);

  // Try 1GB hugepages first, then 2MB ones.
  HugepageSize page_size = HugepageSize::k1GB;

  while (total_free_bytes_ < size) {
    size_t page_bytes = static_cast<size_t>(page_size);
    void *ptr = AllocHugepageFromSocket(page_size, socket_id);

    if (ptr != nullptr) {
      total_free_bytes_ += page_bytes;
      AddRegion(reinterpret_cast<uintptr_t>(ptr), page_bytes);
      pages_.push_back(ptr);
    } else {
      if (page_size == HugepageSize::k1GB) {
        page_size = HugepageSize::k2MB;
      } else {
        break;
      }
    }
  }

  if (total_free_bytes_ >= size) {
    initialized_ = true;
    return;
  }

  // Failed. Give up and clean up.
  for (auto *ptr : pages_) {
    FreeHugepage(ptr);
  }
  pages_.clear();
}

DmaMemoryPool::~DmaMemoryPool() {
  for (auto *ptr : pages_) {
    FreeHugepage(ptr);
  }

  if (!alloced_.empty()) {
    LOG(WARNING) << "DmaMemoryPool " << this << " still has " << alloced_.size()
                 << " unfreed blocks!";
  }
}

void *DmaMemoryPool::Alloc(size_t size) {
  CHECK_GT(size, 0);
  size = align_ceil(size, 4096);
  void *ret = nullptr;

  // Perform first fit allocation.
  // TODO(sangjin): best fit should be better.
  for (const auto &region : regions_) {
    if (region.size >= size) {
      ret = reinterpret_cast<void *>(region.addr);
      alloced_.emplace(ret, size);
      regions_.erase(region);

      if (size < region.size) {
        // there is leftover
        regions_.insert(
            {.addr = region.addr + size, .size = region.size - size});
      }

      total_free_bytes_ -= size;
      break;
    }
  }

  return ret;
}

std::pair<void *, size_t> DmaMemoryPool::AllocUpto(size_t size) {
  CHECK_GT(size, 0);
  size_t aligned_size = align_ceil(size, 4096);
  if (void *ret = Alloc(aligned_size)) {
    return {ret, size};
  }

  // find the largest free region
  const auto it = std::max_element(
      regions_.begin(), regions_.end(),
      [](const auto &a, const auto &b) { return a.size < b.size; });

  if (it == regions_.end()) {
    return {nullptr, 0};
  }

  CHECK_LE(it->size, aligned_size);
  auto ret = std::pair{reinterpret_cast<void *>(it->addr), it->size};
  alloced_.insert(ret);
  regions_.erase(it);
  total_free_bytes_ -= it->size;
  return ret;
}

void DmaMemoryPool::Free(void *ptr) {
  // allow null pointers
  if (!ptr) {
    return;
  }

  const auto &it = alloced_.find(ptr);
  CHECK(it != alloced_.end()) << "Unknown pointer " << ptr;
  const size_t size = (*it).second;
  AddRegion(reinterpret_cast<uintptr_t>(ptr), size);
  alloced_.erase(it);
  total_free_bytes_ += size;
}

std::string DmaMemoryPool::Dump() {
  std::ostringstream out;
  int i = 0;

  out << "DmaMemoryPool at " << this << ": (" << alloced_.size()
      << " alive objects)" << std::endl;

  for (const auto &region : regions_) {
    void *ptr = reinterpret_cast<void *>(region.addr);
    out << utils::Format("  free segment %02d  vaddr 0x%016" PRIxPTR
                         "  paddr 0x%016" PRIxPTR "  size 0x%08zx (%zu)",
                         i++, region.addr, Virt2Phy(ptr), region.size,
                         region.size)
        << std::endl;
  }

  return out.str();
}

// Add a new region, and splice it with adjacent regions if necessary
void DmaMemoryPool::AddRegion(uintptr_t addr, size_t size) {
  ContiguousRegion new_region = {.addr = addr, .size = size};

  auto prev_it = regions_.lower_bound(new_region);
  auto next_it = prev_it;

  if (prev_it != regions_.begin()) {
    prev_it--;
  }

  if (prev_it != next_it && prev_it != regions_.end()) {
    ContiguousRegion prev = *prev_it;
    if (prev.addr + prev.size > new_region.addr) {
      std::cout << prev.addr << ' ' << prev.size << std::endl;
      std::cout << addr << ' ' << size << std::endl;
      if (next_it != regions_.end()) {
        ContiguousRegion next = *next_it;
        std::cout << next.addr << ' ' << next.size << std::endl;
      }
    }

    CHECK_LE(prev.addr + prev.size, new_region.addr);

    if (prev.addr + prev.size == new_region.addr) {
      // Merge with the previous region
      new_region.addr = prev.addr;
      new_region.size += prev.size;
      regions_.erase(prev_it);
    }
  }

  if (next_it != regions_.end()) {
    ContiguousRegion next = *next_it;
    CHECK_LE(new_region.addr + new_region.size, next.addr);

    if (new_region.addr + new_region.size == next.addr) {
      // Merge with the next region
      new_region.size += next.size;
      regions_.erase(next_it);
    }
  }

  regions_.insert(new_region);
}

}  // namespace bess
