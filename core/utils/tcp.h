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

#ifndef BESS_UTILS_TCP_H_
#define BESS_UTILS_TCP_H_

namespace bess {
namespace utils {

// A basic TCP header definition loosely based on the BSD version.
struct[[gnu::packed]] Tcp {
  enum Flag : uint8_t {
    kFin = 0x01,
    kSyn = 0x02,
    kRst = 0x04,
    kPsh = 0x08,
    kAck = 0x10,
    kUrg = 0x20,
  };

  be16_t src_port;  // Source port.
  be16_t dst_port;  // Destination port.
  be32_t seq_num;   // Sequence number.
  be32_t ack_num;   // Acknowledgement number.
#if __BYTE_ORDER == __LITTLE_ENDIAN
  uint8_t reserved : 4;  // Unused reserved bits.
  uint8_t offset : 4;    // Data offset.
#elif __BYTE_ORDER == __BIG_ENDIAN
  uint8_t offset : 4;    // Data offset.
  uint8_t reserved : 4;  // Unused reserved bits.
#else
#error __BYTE_ORDER must be defined.
#endif
  uint8_t flags;      // Flags.
  be16_t window;      // Receive window.
  uint16_t checksum;  // Checksum.
  be16_t urgent_ptr;  // Urgent pointer.
};

static_assert(std::is_pod<Tcp>::value, "not a POD type");
static_assert(sizeof(Tcp) == 20, "struct Tcp is incorrect");

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_TCP_H_
