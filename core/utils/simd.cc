#include "simd.h"

#include "format.h"

std::string m128i_to_str(__m128i a) {
  union {
    __m128i vec;
    uint32_t b[4];
  };

  vec = a;
  return bess::utils::Format("[%08x %08x %08x %08x]", b[0], b[1], b[2], b[3]);
}

#if __AVX__

std::string m256i_to_str(__m256i a) {
  union {
    __m256i vec;
    uint32_t b[8];
  };

  vec = a;
  return bess::utils::Format("[%08x %08x %08x %08x %08x %08x %08x %08x]",
                             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
}

#endif
