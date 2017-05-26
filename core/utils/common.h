/* This header file contains general (not BESS specific) C/C++ definitions */

#ifndef BESS_UTILS_COMMON_H_
#define BESS_UTILS_COMMON_H_

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <string>

#if __cplusplus < 201103L  // pre-C++11?
#error The compiler does not support C++11
#endif

// [[maybe_unused]] is a c++17 feature,
// but g++ (>= 4.8) has its own [[gnu::unused]]
#if __cplusplus <= 201402L  // C++14 or older?
#define maybe_unused gnu::unused
#endif

/* Hint for performance optimization. Same as _nDCHECK() of TI compilers */
#define promise(cond)          \
  ({                           \
    if (!(cond))               \
      __builtin_unreachable(); \
  })
#define promise_unreachable() __builtin_unreachable();

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

#define member_type(type, member) typeof(((type *)0)->member)

#define ARR_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

static inline uint64_t align_floor(uint64_t v, uint64_t align) {
  return v - (v % align);
}

static inline uint64_t align_ceil(uint64_t v, uint64_t align) {
  return align_floor(v + align - 1, align);
}

static inline uint64_t align_ceil_pow2(uint64_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v |= v >> 32;

  return v + 1;
}

#define __cacheline_aligned __attribute__((aligned(64)))

/* For x86_64. DMA operations are not safe with these macros */
#define INST_BARRIER() asm volatile("" ::: "memory")
#define LOAD_BARRIER() INST_BARRIER()
#define STORE_BARRIER() INST_BARRIER()
#define FULL_BARRIER() asm volatile("mfence" ::: "memory")

// Put this in the declarations for a class to be uncopyable.
#define DISALLOW_COPY(TypeName) TypeName(const TypeName &) = delete
// Put this in the declarations for a class to be unassignable.
#define DISALLOW_ASSIGN(TypeName) void operator=(const TypeName &) = delete
// A macro to disallow the copy constructor and operator= functions.
// This should be used in the private: declarations for a class.
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName &) = delete;     \
  void operator=(const TypeName &) = delete
// A macro to disallow all the implicit constructors, namely the
// default constructor, copy constructor and operator= functions.
//
// This should be used in the private: declarations for a class
// that wants to prevent anyone from instantiating it. This is
// especially useful for classes containing only static methods.
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName() = delete;                           \
  DISALLOW_COPY_AND_ASSIGN(TypeName)

// Used to explicitly mark the return value of a function as unused. If you are
// really sure you don't want to do anything with the return value of a function
// that has been marked WARN_UNUSED_RESULT, wrap it with this. Example:
//
//   std::unique_ptr<MyType> my_var = ...;
//   if (TakeOwnership(my_var.get()) == SUCCESS)
//     ignore_result(my_var.release());
//
template <typename T>
inline void ignore_result(const T &) {}

// An RAII holder for file descriptors.  When initialized with a file desciptor,
// takes
// ownership of the fd, meaning that when this holder instance is destroyed the
// fd is
// closed.  Primarily useful in unit tests where we want to ensure that previous
// tests
// have been cleaned up before starting new ones.
class unique_fd {
 public:
  // Constructs a unique fd that owns the given fd.
  unique_fd(int fd) : fd_(fd) {}

  // Move constructor
  unique_fd(unique_fd &&other) : fd_(other.fd_) { other.fd_ = -1; }

  // Destructor, which closes the fd if not -1.
  ~unique_fd() {
    if (fd_ != -1) {
      close(fd_);
    }
  }

  // Resets this instance and closes the fd held, if any.
  void reset() {
    if (fd_ != -1) {
      close(fd_);
    }
    fd_ = -1;
  }

  // Releases the held fd from ownership.  Returns -1 if no fd is held.
  int release() {
    int ret = fd_;
    fd_ = -1;
    return ret;
  }

  int get() const { return fd_; }

 private:
  int fd_;

  DISALLOW_COPY_AND_ASSIGN(unique_fd);
};

// Inserts the given item into the container in sorted order.  Starts from the
// end of the container and swaps forward.  Assumes the container is already
// sorted.
template <typename T, typename U>
inline void InsertSorted(T &container, U &item) {
  container.push_back(item);

  for (size_t i = container.size()-1; i > 0; --i) {
    auto &prev = container[i-1];
    auto &cur = container[i];
    if (cur < prev) {
      std::swap(prev, cur);
    } else {
      break;
    }
  }
}

// Returns the absolute difference between `lhs` and `rhs`.
template <typename T>
T absdiff(const T &lhs, const T &rhs) {
  return lhs > rhs ? lhs - rhs : rhs - lhs;
}

#endif  // BESS_UTILS_COMMON_H_
