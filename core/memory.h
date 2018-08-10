#ifndef BESS_MEMORY_H_
#define BESS_MEMORY_H_

#include <glog/logging.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace bess {

// For the physical/IO address space   0x 00000000000 - 0x fff00000000 (16TB),
// we use the virtual address range    0x600000000000 - 0x6fffffffffff
const uintptr_t kVirtualAddressStart = 0x6000'0000'0000ull;
const uintptr_t kVirtualAddressEnd = 0x7000'0000'0000ull;  // not inclusive

// it violates the naming scheme, but k2Mb would sound like bits, not bytes..
enum class HugepageSize : size_t {
  k2MB = 1 << 21,
  k1GB = 1 << 30,
};

// Assumes a single node system if undetectable
int NumNumaNodes();

HugepageSize GetDefaultHugepageSize();

// Translate a virtual address of this process into a physical one.
// Unlike Virt2Phy(), the underlying page doesn't need to be a hugepage.
// (but still the pointer should be a valid one)
// Returns 0 if failed: invalid virtual address, no CAP_SYS_ADMIN, etc.
// This function is very slow -- never meant to be used in the datapath.
uintptr_t Virt2PhyGeneric(void *ptr);

// Same as Virt2PhyGeneric(), but much faster. Only valid for memory blocks
// allocated by AllocHugepage() or DmaMemoryPool::Alloc()
static inline uintptr_t Virt2Phy(void *ptr) {
  uintptr_t vaddr = reinterpret_cast<uintptr_t>(ptr);
  DCHECK(kVirtualAddressStart <= vaddr);
  DCHECK(vaddr < kVirtualAddressEnd);
  return vaddr ^ kVirtualAddressStart;
}

// Only valid for memory blocks allocated by AllocHugepage() or
// DmaMemoryPool::Alloc()
static inline void *Phy2Virt(uintptr_t paddr) {
  DCHECK(paddr < (kVirtualAddressEnd - kVirtualAddressStart));
  return reinterpret_cast<void *>(paddr + kVirtualAddressStart);
}

// Allocate/deallocate a hugepage backed by physical memory.
// Suitable for DMA. The page is zero-initialized by the kernel
// These functions are quite low level and you probably won't need them;
// use DmaMemoryPool::Alloc()/Free() below instead.
void *AllocHugepage(HugepageSize type);
void FreeHugepage(void *ptr);

// Same as AllocHugepage, but from a specified NUMA node
void *AllocHugepageFromSocket(HugepageSize type, int socket_id);

// Manages a set of hugepages for allocation of memory blocks.
// You can allocate/deallocate a memory region that is contiguous in both
// physical/virtual address spaces.
// This is not meant to be a fast memory allocator. It's only suitable for
// infrequently allocated/freed, large objects (e.g., packet pools).
class DmaMemoryPool {
 public:
  // Memory from any NUMA node can be allocated if socket_id == -1.
  DmaMemoryPool(size_t size, int socket_id = -1);
  virtual ~DmaMemoryPool();

  // Return true if fully initialized. false if constructor failed
  bool Initialized() const { return initialized_; }

  // The socket ID this mempool is associated with. -1 means unknown.
  int SocketId() const { return socket_id_; }

  // Returns a contiguous memory block from the pool, or nullptr if failed.
  // All returned addresses are 4K-aligned.
  void *Alloc(size_t size);

  // Same as Alloc(), but it may allocate memory block smaller than specified
  // if no such free space is available. 0 <= returned_size <= size.
  std::pair<void *, size_t> AllocUpto(size_t size);

  void Free(void *ptr);

  size_t TotalFreeBytes() const { return total_free_bytes_; }

  // Return human-readable debug messages
  std::string Dump();

 private:
  struct ContiguousRegion {
    uintptr_t addr;
    size_t size;

    bool operator<(const ContiguousRegion &o) const { return addr < o.addr; }
  };

  void AddRegion(uintptr_t addr, size_t size);

  // current list of contiguous memory regions
  std::set<ContiguousRegion> regions_;

  // all hugepages backing this pool
  std::vector<void *> pages_;

  // keeps track of allocated memory blocks and their size
  std::map<void *, size_t> alloced_;

  bool initialized_;
  int socket_id_;
  size_t total_free_bytes_;
};

}  // namespace bess

#endif  // BESS_MEMORY_H_
