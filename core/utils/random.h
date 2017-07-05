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

#ifndef BESS_UTILS_RANDOM_H_
#define BESS_UTILS_RANDOM_H_

#include <cstdint>

#include "time.h"

class Random {
 public:
  Random() : seed_(rdtsc()) {}
  explicit Random(uint64_t seed) : seed_(seed) {}

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
