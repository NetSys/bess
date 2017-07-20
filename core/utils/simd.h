// Copyright (c) 2014-2016, The Regents of the University of California.
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

#ifndef BESS_UTILS_SIMD_H_
#define BESS_UTILS_SIMD_H_

#include <string>

#include <x86intrin.h>

#include <glog/logging.h>

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
  DCHECK(i == 0 || i == 1) << "selector must be either 0 or 1";

  // While this looks silly, otherwise g++ will complain on -O0
  if (i == 0) {
    return _mm_extract_epi64(a, 0);
  } else {
    return _mm_extract_epi64(a, 1);
  }
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
