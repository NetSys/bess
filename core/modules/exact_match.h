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

#ifndef BESS_MODULES_EXACTMATCH_H_
#define BESS_MODULES_EXACTMATCH_H_

#include <rte_config.h>
#include <rte_hash_crc.h>

#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../utils/cuckoo_map.h"

#define MAX_FIELDS 8
#define MAX_FIELD_SIZE 8

static_assert(MAX_FIELD_SIZE <= sizeof(uint64_t),
              "field cannot be larger than 8 bytes");

#define HASH_KEY_SIZE (MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

using google::protobuf::RepeatedPtrField;
using bess::utils::HashResult;
using bess::utils::CuckooMap;

struct em_hkey_t {
  uint64_t u64_arr[MAX_FIELDS];
};

class em_eq {
 public:
  explicit em_eq(size_t len) : len_(len) {}

  bool operator()(const em_hkey_t &lhs, const em_hkey_t &rhs) const {
    promise(len_ >= sizeof(uint64_t));
    promise(len_ <= sizeof(em_hkey_t));

    for (size_t i = 0; i < len_ / 8; i++) {
      if (lhs.u64_arr[i] != rhs.u64_arr[i]) {
        return false;
      }
    }
    return true;
  }

 private:
  size_t len_;
};

class em_hash {
 public:
  explicit em_hash(size_t len) : len_(len) {}

  HashResult operator()(const em_hkey_t &key) const {
    HashResult init_val = 0;

    promise(len_ >= sizeof(uint64_t));
    promise(len_ <= sizeof(em_hkey_t));

#if __SSE4_2__ && __x86_64
    for (size_t i = 0; i < len_ / 8; i++) {
      init_val = crc32c_sse42_u64(key.u64_arr[i], init_val);
    }
    return init_val;
#else
    return rte_hash_crc(&key, len_, init_val);
#endif
  }

 private:
  size_t len_;
};

typedef CuckooMap<em_hkey_t, gate_idx_t, em_hash, em_eq> htable_t;

struct EmField {
  /* bits with 1: the bit must be considered.
   * bits with 0: don't care */
  uint64_t mask;

  int attr_id; /* -1 for offset-based fields */

  /* Relative offset in the packet data for offset-based fields.
   *  (starts from data_off, not the beginning of the headroom */
  int offset;

  int pos; /* relative position in the key */

  int size; /* in bytes. 1 <= size <= MAX_FIELD_SIZE */
};

class ExactMatch final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;

  ExactMatch()
      : Module(),
        default_gate_(),
        total_key_size_(),
        num_fields_(),
        fields_(),
        ht_() {
    max_allowed_workers_ = Worker::kMaxWorkers;
  }

  void ProcessBatch(bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

  CommandResponse Init(const bess::pb::ExactMatchArg &arg);
  CommandResponse CommandAdd(const bess::pb::ExactMatchCommandAddArg &arg);
  CommandResponse CommandDelete(
      const bess::pb::ExactMatchCommandDeleteArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);
  CommandResponse CommandSetDefaultGate(
      const bess::pb::ExactMatchCommandSetDefaultGateArg &arg);

 private:
  CommandResponse AddFieldOne(const bess::pb::Field &field,
                              const bess::pb::FieldData &mask,
                              struct EmField *f, int idx);
  CommandResponse GatherKey(const RepeatedPtrField<bess::pb::FieldData> &fields,
                            em_hkey_t *key);

  gate_idx_t default_gate_;

  uint32_t total_key_size_;

  int num_fields_;
  EmField fields_[MAX_FIELDS];

  htable_t ht_;
};

#endif  // BESS_MODULES_EXACTMATCH_H_
