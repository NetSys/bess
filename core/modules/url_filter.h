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

#ifndef BESS_MODULES_URL_FILTER_H_
#define BESS_MODULES_URL_FILTER_H_

#include <rte_config.h>
#include <rte_hash_crc.h>

#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../module.h"
#include "../packet.h"
#include "../pb/module_msg.pb.h"
#include "../utils/tcp_flow_reconstruct.h"
#include "../utils/trie.h"

using bess::utils::TcpFlowReconstruct;
using bess::utils::Trie;
using bess::utils::be16_t;
using bess::utils::be32_t;

// A helper class that defines a TCP flow
class alignas(16) Flow {
 public:
  be32_t src_ip;
  be32_t dst_ip;
  be16_t src_port;
  be16_t dst_port;
  uint32_t padding;

  Flow() : padding(0) {}

  bool operator==(const Flow &other) const {
    return memcmp(this, &other, sizeof(*this)) == 0;
  }
};

static_assert(sizeof(Flow) == 16, "Flow must be 16 bytes.");

// Hash function for std::unordered_map
struct FlowHash {
  std::size_t operator()(const Flow &f) const {
    uint32_t init_val = 0;

#if __SSE4_2__ && __x86_64
    const union {
      Flow flow;
      uint64_t u64[2];
    } &bytes = {.flow = f};

    init_val = crc32c_sse42_u64(bytes.u64[0], init_val);
    init_val = crc32c_sse42_u64(bytes.u64[1], init_val);
#else
    init_val = rte_hash_crc(&f, sizeof(Flow), init_val);
#endif

    return init_val;
  }
};

class FlowRecord {
 public:
  FlowRecord() : buffer_(128), expiry_time_(0) {}

  TcpFlowReconstruct &GetBuffer() { return buffer_; }
  uint64_t ExpiryTime() { return expiry_time_; }
  void SetExpiryTime(uint64_t time) { expiry_time_ = time; }

 private:
  TcpFlowReconstruct buffer_;
  uint64_t expiry_time_;
};

// A module of HTTP URL filtering. Ends an HTTP connection if the Host field
// matches the blacklist.
// igate/ogate 0: traffic from internal network to external network
// igate/ogate 1: traffic from external network to internal network
class UrlFilter final : public Module {
 public:
  typedef std::pair<std::string, std::string> Url;

  static const Commands cmds;
  static const gate_idx_t kNumIGates = 2;
  static const gate_idx_t kNumOGates = 2;

  CommandResponse Init(const bess::pb::UrlFilterArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const bess::pb::UrlFilterArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

 private:
  std::unordered_map<std::string, Trie<std::tuple<>>> blacklist_;
  std::unordered_map<Flow, FlowRecord, FlowHash> flow_cache_;
};

#endif  // BESS_MODULES_URL_FILTER_H_
