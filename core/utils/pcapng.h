// Copyright (c) 2017, Nefeli Networks, Inc.
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

#ifndef BESS_UTILS_PCAPNG_H_
#define BESS_UTILS_PCAPNG_H_

namespace bess {
namespace utils {
namespace pcapng {

// This code is based on the specification from
// https://pcapng.github.io/pcapng/

// A pcapng file is simply one or more sections concatenated. Each section is
// composed by blocks. Each block is a TLV structure with the length repeated
// at the at the beginning and at the end, so that unknown block can be skipped
// and the file can be traversed backwards. The block length includes the
// header.

// Must be placed at the beginning of each section.  Since a pcapng file
// starts directly with a section, it also serves the purpose of identifying
// the file type for tools like `file`.
struct SectionHeaderBlock {
  uint32_t type;     // kType
  uint32_t tot_len;  // Block Total Length (hdr + opts + repeated tot_len)
  uint32_t bom;      // kBom
  uint16_t maj_ver;  // kMajVer
  uint16_t min_ver;  // kMinVer
  int64_t sec_len;   // Section Length or -1
  // Options...
  // uint32_t tot_len     // Repeated

  static constexpr uint32_t kType = 0x0A0D0D0A;
  static constexpr uint32_t kBom = 0x1A2B3C4D;
  static constexpr uint32_t kMajVer = 1;
  static constexpr uint32_t kMinVer = 0;
};

// Before including any packet block, a section must incluse at least one
// of these to provide information about the interfaces.
struct InterfaceDescriptionBlock {
  uint32_t type;       // kType
  uint32_t tot_len;    // Block Total Length (hdr + opts + repeated tot_len)
  uint16_t link_type;  // One of LinkType
  uint16_t reserved;   // 0/Ignored
  uint32_t snap_len;   // Maximum number of bytes captured on a packet
  // Options...
  // uint32_t tot_len     // Repeated

  enum LinkType {
    kEthernet = 1,
  };

  static constexpr uint32_t kType = 0x00000001;
};

// Stores a packet.
struct EnhancedPacketBlock {
  uint32_t type;            // kType
  uint32_t tot_len;         // Block Total Length (hdr + pkt data +
                            //                     opts + repeated tot_len)
  uint32_t interface_id;    // Index of the InterfaceDescriptionBlock
  uint32_t timestamp_high;  // Most significant 32-bit of the 64-bit timestamp
  uint32_t timestamp_low;   // Least significant 32-bit of the 64-bit timestamp
  uint32_t captured_len;    // Length of the packet data in this block
  uint32_t orig_len;        // Original length of the packet on the wire
  // Packet data (padded to 32-bit)
  // Options...
  // uint32_t tot_len     // Repeated

  static constexpr uint32_t kType = 0x00000006;
};

// Most block types can be extended with options. Options are TLV structures.
// Unlike block header, the option doesn't repeat the length, and the length
// length doesn't account for the header itself.
struct Option {
  uint16_t code;  // One of Code
  uint16_t len;   // Length of the value (not including the header)
  // Option value (padded to 32-bit)

  enum Code {
    kEndOfOpts = 0,  // Must always be the last option.  len=0
    kComment = 1,    // UTF-8 string.  Not zero terminated.
  };
};

}  // namespace pcapng
}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_PCAPNG_H_
