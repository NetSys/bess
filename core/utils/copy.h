#ifndef BESS_UTILS_COPY_H_
#define BESS_UTILS_COPY_H_

#include <cstring>

#include <x86intrin.h>

#include <glog/logging.h>

#include "common.h"

namespace bess {
namespace utils {

static inline void Copy16(void *__restrict__ dst,
                          const void *__restrict__ src) {
  _mm_storeu_si128(reinterpret_cast<__m128i *>(dst),
                   _mm_loadu_si128(reinterpret_cast<const __m128i *>(src)));
}

static inline void Copy32(void *__restrict__ dst,
                          const void *__restrict__ src) {
#if __AVX2__
  _mm256_storeu_si256(
      reinterpret_cast<__m256i *>(dst),
      _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src)));
#else
  Copy16(dst, src);
  Copy16(reinterpret_cast<__m128i *>(dst) + 1,
         reinterpret_cast<const __m128i *>(src) + 1);
#endif
}

// Copy exactly "bytes" (<= 64). Works best if size is a compile-time constant.
static inline void CopySmall(void *__restrict__ dst,
                             const void *__restrict__ src, size_t bytes) {
  DCHECK(bytes <= 64);

  auto *d = reinterpret_cast<char *__restrict__>(dst);
  auto *s = reinterpret_cast<const char *__restrict__>(src);

  if (32 < bytes) {
    Copy32(d, s);
    Copy32(d + bytes - 32, s + bytes - 32);
    return;
  }

  if (16 < bytes) {
    Copy16(d, s);
    Copy16(d + bytes - 16, s + bytes - 16);
    return;
  }

  switch (bytes) {
    case 16:
      Copy16(d, s);
      break;
    case 15:
      memcpy(d, s, 8);
      memcpy(d + 7, s + 7, 8);
      break;
    case 14:
      memcpy(d, s, 8);
      memcpy(d + 6, s + 6, 8);
      break;
    case 13:
      memcpy(d, s, 8);
      memcpy(d + 5, s + 5, 8);
      break;
    case 12:
      memcpy(d, s, 12);
      break;
    case 11:
      memcpy(d, s, 8);
      memcpy(d + 7, s + 7, 4);
      break;
    case 10:
      memcpy(d, s, 10);
      break;
    case 9:
      memcpy(d + 8, s + 8, 1);
    case 8:
      memcpy(d, s, 8);
      break;
    case 7:
      memcpy(d, s, 4);
      memcpy(d + 3, s + 3, 4);
      break;
    case 6:
      memcpy(d, s, 6);
      break;
    case 5:
      memcpy(d + 4, s + 4, 1);
    case 4:
      memcpy(d, s, 4);
      break;
    case 3:
      memcpy(d + 2, s + 2, 1);
    case 2:
      memcpy(d, s, 2);
      break;
    case 1:
      memcpy(d, s, 1);
      break;
  }
}

// Inline version of Copy(). Use only when performance is critial. Since the
// function is inlined whenever used, the compiled code will be substantially
// larger. See Copy() for more details.
static inline void CopyInlined(void *__restrict__ dst,
                               const void *__restrict__ src, size_t bytes,
                               bool sloppy = false) {
#if __AVX2__
  using block_t = __m256i;
  auto copy_block = [](void *__restrict__ d, const void *__restrict__ s) {
    Copy32(d, s);
  };
#else
  using block_t = __m128i;
  auto copy_block = [](void *__restrict__ d, const void *__restrict__ s) {
    Copy16(d, s);
  };
#endif

  const size_t block_size = sizeof(block_t);
  uintptr_t dst_u = reinterpret_cast<uintptr_t>(dst);
  uintptr_t src_u = reinterpret_cast<uintptr_t>(src);

  if (bytes <= 64 && !sloppy) {
    CopySmall(dst, src, bytes);
    return;
  }

  // Align dst on a cache line if buffer is big yet misaligned.
  if (bytes >= 256 && (dst_u % block_size) != 0) {
    // Copy "block_t" bytes, but proceed with only "offset" bytes.
    copy_block(reinterpret_cast<block_t *__restrict__>(dst),
               reinterpret_cast<const block_t *__restrict__>(src));

    uintptr_t offset = block_size - (dst_u % block_size);
    dst = reinterpret_cast<decltype(dst)>(dst_u + offset);
    src = reinterpret_cast<decltype(src)>(src_u + offset);
    bytes -= offset;
  }

  auto *d = reinterpret_cast<block_t *__restrict__>(dst);
  auto *s = reinterpret_cast<const block_t *__restrict__>(src);

  size_t num_blocks = (sloppy ? bytes + block_size - 1 : bytes) / block_size;
  size_t num_loops = num_blocks / 8;

  while (num_loops--) {
    copy_block(d + 0, s + 0);
    copy_block(d + 1, s + 1);
    copy_block(d + 2, s + 2);
    copy_block(d + 3, s + 3);
    copy_block(d + 4, s + 4);
    copy_block(d + 5, s + 5);
    copy_block(d + 6, s + 6);
    copy_block(d + 7, s + 7);
    d += 8;
    s += 8;
  }

  // Copy the leftover. No block to copy if remainder is 0
  size_t leftover_blocks = num_blocks % 8;

  switch (leftover_blocks) {
    case 7:
      copy_block(d + 6, s + 6);
    case 6:
      copy_block(d + 5, s + 5);
    case 5:
      copy_block(d + 4, s + 4);
    case 4:
      copy_block(d + 3, s + 3);
    case 3:
      copy_block(d + 2, s + 2);
    case 2:
      copy_block(d + 1, s + 1);
    case 1:
      copy_block(d + 0, s + 0);
  }

  if (!sloppy && (bytes % block_size) != 0) {
    size_t fringe = bytes % block_size;
    dst_u = reinterpret_cast<uintptr_t>(d + leftover_blocks);
    src_u = reinterpret_cast<uintptr_t>(s + leftover_blocks);

    copy_block(reinterpret_cast<decltype(d)>(dst_u + fringe - block_size),
               reinterpret_cast<decltype(s)>(src_u + fringe - block_size));
  }
}

// Non-inlined version of Copy().
// Do not call this function directly, unless you know what you are doing.
// Just use Copy()
void CopyNonInlined(void *__restrict__ dst, const void *__restrict__ src,
                    size_t bytes, bool sloppy = false);

// Copies "bytes" data from "src" to "dst".
// Same as memcpy() and rte_memcpy(), but significantly faster for both
// aligned/unaligned buffers. Performs best if aligned, of course.
// bytes can be 0.
//
// NOTE: When "sloppy" is set, it may copy more than "bytes", up to additional
// 31 bytes. It will generate much smaller and usually faster code. Use this
// option only if overwriting some data at the end is acceptable, such as
// rewriting the payload data of class Packet.
static inline void Copy(void *__restrict__ dst, const void *__restrict__ src,
                        size_t bytes, bool sloppy = false) {
  // If the size is a compile-time constant, inlining can generate compact code
  if (__builtin_constant_p(bytes)) {
    CopyInlined(dst, src, bytes, sloppy);
  } else {
    CopyNonInlined(dst, src, bytes, sloppy);
  }
}

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_COPY_H_
