#ifndef BESS_UTILS_RANDOM_H_
#define BESS_UTILS_RANDOM_H_

#include <cstdint>

#include "time.h"

class Random {
 public:
  Random() : seed_(rdtsc()) {}
  Random(uint64_t seed) : seed_(seed) {}

  void SetSeed(uint64_t seed) { this->seed_ = seed; };

  uint32_t Get();
  uint32_t GetRange(uint32_t range);
  double GetReal();
  double GetRealNonzero();

 private:
  uint64_t seed_;
};

inline uint32_t Random::Get() {
  seed_ = seed_ * 1103515245 + 12345;
  return seed_ >> 32;
}

/* returns [0, range) with no integer modulo operation */
inline uint32_t Random::GetRange(uint32_t range) {
  union {
    uint64_t i;
    double d;
  } tmp;

  /*
   * From the MSB,
   * 0: sign
   * 1-11: exponent (0x3ff == 0, 0x400 == 1)
   * 12-63: mantissa
   * The resulting double number is 1.(b0)(b1)...(b47),
   * where seed_ is (b0)(b1)...(b63).
   */
  seed_ = seed_ * 1103515245 + 12345;
  tmp.i = (seed_ >> 12) | 0x3ff0000000000000ul;
  return (tmp.d - 1.0) * range;
}

/* returns [0.0, 1.0) */
inline double Random::GetReal() {
  union {
    uint64_t i;
    double d;
  } tmp;

  seed_ = seed_ * 1103515245 + 12345;
  tmp.i = (seed_ >> 12) | 0x3ff0000000000000ul;
  return tmp.d - 1.0;
}

/* returns (0.0, 1.0] (note it includes 1.0) */
inline double Random::GetRealNonzero() {
  union {
    uint64_t i;
    double d;
  } tmp;

  seed_ = seed_ * 1103515245 + 12345;
  tmp.i = (seed_ >> 12) | 0x3ff0000000000000ul;
  return 2.0 - tmp.d;
}

#endif  // BESS_UTILS_RANDOM_H_
