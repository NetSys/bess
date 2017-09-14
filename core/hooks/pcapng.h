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

#ifndef BESS_HOOKS_PCAPNG_
#define BESS_HOOKS_PCAPNG_

#include "../message.h"
#include "../module.h"

// Pcapng dumps copies of the packets seen by a gate (data + metadata) in
// pcapng format.  Useful for debugging.
class Pcapng final : public bess::GateHook {
 public:
  Pcapng();

  virtual ~Pcapng();

  CommandResponse Init(const bess::Gate *, const bess::pb::PcapngArg &);

  void ProcessBatch(const bess::PacketBatch *batch);

  static constexpr uint16_t kPriority = 2;
  static const std::string kName;

 private:
  struct Attr {
    // Attribute offset in the packet metadata.
    int md_offset;
    // Size in bytes of the attribute.
    size_t size;
    // Offset where this attribute hex dump should go inside `attr_template_`.
    size_t tmpl_offset;
  };

  // The file descripton where to output the pcapng stream.
  int fifo_fd_;
  // List of attributes to dump.
  std::vector<Attr> attrs_;
  // Preallocated string with attribute names and values.  For each packet,
  // we will change in place the values and send the string out, without
  // doing any memory allocation.
  std::vector<char> attr_template_;
};

#endif  // BESS_HOOKS_PCAPNG_
