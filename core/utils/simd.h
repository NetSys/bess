#ifndef BESS_UTILS_SIMD_H_
#define BESS_UTILS_SIMD_H_

#include <string>

#include <x86intrin.h>

#define __xmm_aligned __attribute__((aligned(16)))
#define __ymm_aligned __attribute__((aligned(32)))
#define __zmm_aligned __attribute__((aligned(64)))

#if !__SSSE3__
#error CPU must be at least Core 2 or equivalent (SSSE3 required)
#endif

std::string m128i_to_str(__m128i a);

static inline __m128i gather_m128i(void *a, void *b) {
#if 1
  /* faster (in a tight loop test. sometimes slower...) */
  __m128i t = _mm_loadl_epi64((__m128i *)a);
  return (__m128i)_mm_loadh_pd((__m128d)t, (double *)b);
#else
  return _mm_set_epi64x(*((uint64_t *)b), *((uint64_t *)a));
#endif
}

#if __AVX__

std::string m256i_to_str(__m256i a);

static inline __m256d concat_two_m128d(__m128d lo, __m128d hi) {
#if 1
  /* faster */
  return _mm256_insertf128_pd(_mm256_castpd128_pd256(lo), hi, 1);
#else
  return _mm256_permute2f128_si256(_mm256_castsi128_si256(lo),
                                   _mm256_castsi128_si256(hi), (2 << 4) | 0);
#endif
}

static inline __m256i concat_two_m128i(__m128i lo, __m128i hi) {
#if __AVX2__
  return _mm256_inserti128_si256(_mm256_castsi128_si256(lo), hi, 1);
#else
  return _mm256_insertf128_si256(_mm256_castsi128_si256(lo), hi, 1);
#endif
}

static inline uint64_t m128i_extract_u64(__m128i a, int i) {
#if __x86_64
  return _mm_extract_epi64(a, i);
#else
  // In 32-bit machines, _mm_extract_epi64() is not supported
  union {
    __m128i vec;
    uint64_t b[2];
  };

  vec = a;
  return b[i];
#endif
}

#endif  // __AVX__

#endif  // BESS_UTILS_SIMD_H_
