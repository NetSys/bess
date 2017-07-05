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

#include "ether.h"

#include <cstring>
#include <sstream>

#include "copy.h"
#include "endian.h"
#include "format.h"
#include "random.h"

namespace bess {
namespace utils {

using Address = Ethernet::Address;

Address::Address(const std::string &str) {
  if (!FromString(str)) {
    memset(bytes, 0, sizeof(bytes));
  };
}

bool Address::FromString(const std::string &str) {
  int ret = Parse(str, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", &bytes[0],
                  &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]);
  return (ret == Address::kSize);
}

std::string Address::ToString() const {
  return Format("%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", bytes[0], bytes[1],
                bytes[2], bytes[3], bytes[4], bytes[5]);
}

void Address::Randomize() {
  Random rng;

  for (size_t i = 0; i < Address::kSize; i++) {
    bytes[i] = rng.Get() & 0xff;
  }

  bytes[0] &= 0xfe;  // not broadcast/multicast
  bytes[1] |= 0x02;  // locally administered
}

}  // namespace utils
}  // namespace bess
