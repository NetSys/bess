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

#ifndef BESS_UTILS_EXACT_MATCH_TABLE_H_
#define BESS_UTILS_EXACT_MATCH_TABLE_H_

#include <string>
#include <type_traits>
#include <vector>

#include <rte_config.h>
#include <rte_hash_crc.h>

#include "../message.h"
#include "../metadata.h"
#include "../module.h"
#include "../packet.h"
#include "bits.h"
#include "cuckoo_map.h"
#include "endian.h"
#include "format.h"

#define MAX_FIELDS 8
#define MAX_FIELD_SIZE 8

static_assert(MAX_FIELD_SIZE <= sizeof(uint64_t),
              "field cannot be larger than 8 bytes");

#define HASH_KEY_SIZE (MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

namespace bess {
namespace utils {

using Error = std::pair<int, std::string>;

// ExactMatchKey is a composite key made up of values extracted from a buffer
// according to a collection of ExactMatchFields, and should not be created by
// hand. Use ExactMatchTable::MakeKey() and ExactMatchTable::MakeKeys() to
// create a key.
struct ExactMatchKey {
  uint64_t u64_arr[MAX_FIELDS];
};

// Equality operator for two ExactMatchKeys
class ExactMatchKeyEq {
 public:
  explicit ExactMatchKeyEq(size_t len) : len_(len) {}

  bool operator()(const ExactMatchKey &lhs, const ExactMatchKey &rhs) const {
    promise(len_ >= sizeof(uint64_t));
    promise(len_ <= sizeof(ExactMatchKey));

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

// Hash function for an ExactMatchKey
class ExactMatchKeyHash {
 public:
  explicit ExactMatchKeyHash(size_t len) : len_(len) {}

  HashResult operator()(const ExactMatchKey &key) const {
    HashResult init_val = 0;

    promise(len_ >= sizeof(uint64_t));
    promise(len_ <= sizeof(ExactMatchKey));

#if __x86_64
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

// ExactMatchField describes a field to be matched on.
// These should only be manually created when calling
// ExactMatchTable::AddField(). In this case, only the `mask`, `offset`, and
// `size` fields will be considered. `pos` and `attr_id` are set by
// ExactMatchTable::AddField().
struct ExactMatchField {
  // bits with 1: the bit must be considered.
  // bits with 0: don't care
  uint64_t mask;

  int attr_id;  // -1 for offset-based fields

  // Relative offset in the packet data for offset-based fields.
  // (starts from data_off, not the beginning of the headroom
  int offset;

  int pos;  // relative position in the key

  int size;  // in bytes. 1 <= size <= MAX_FIELD_SIZE
};

static_assert(std::is_pod<ExactMatchField>::value,
              "ExactMatchField is not a POD type");

// ExactMatchRuleFields specifies the values for each of the fields defined in
// an ExactMatchTable for a rule.  For example, if your ExactMatchTable was
// configured with two fields of length 4 and 2 bytes respectively, the
// ExactMatchRuleFields for a rule [0x89_AB_CD, 0xEF] would be
// {{0xCD, 0xAB, 0x89}, {0xEF}} (the code assumes a little endian architecture).
typedef std::vector<std::vector<uint8_t>> ExactMatchRuleFields;

// ExactMatchTable operates as a sort-of extended CuckooMap.
// It allows you to map multiple fields (e.g., packet headers), to some type T
// (e.g., a gate index).
template <typename T>
class ExactMatchTable {
 public:
  using EmTable =
      CuckooMap<ExactMatchKey, T, ExactMatchKeyHash, ExactMatchKeyEq>;

  ExactMatchTable()
      : raw_key_size_(),
        total_key_size_(),
        num_fields_(),
        fields_(),
        table_() {}

  // Add a new rule.
  //
  // @param fields
  //  The field values to match on (vector of bytestrings).
  // @param val
  //  The value to associate with packets that match this rule.
  // Returns 0 on success, non-zero errno on failure.
  Error AddRule(const T &val, const ExactMatchRuleFields &fields) {
    ExactMatchKey key;
    Error err;

    if (fields.size() == 0) {
      return MakeError(EINVAL, "rule has no fields");
    }

    if ((err = gather_key(fields, &key)).first != 0) {
      return err;
    }

    table_.Insert(key, val, ExactMatchKeyHash(total_key_size_),
                  ExactMatchKeyEq(total_key_size_));

    return MakeError(0);
  }

  // Delete an existing rule.
  //
  // @param fields
  //  The field values of the rule to remove (vector of bytestrings)
  // Returns 0 on success, non-zero errno on failure.
  Error DeleteRule(const ExactMatchRuleFields &fields) {
    ExactMatchKey key;
    Error err;

    if (fields.size() == 0) {
      return MakeError(EINVAL, "rule has no fields");
    }

    if ((err = gather_key(fields, &key)).first != 0) {
      return err;
    }

    bool ret = table_.Remove(key, ExactMatchKeyHash(total_key_size_),
                             ExactMatchKeyEq(total_key_size_));
    if (!ret) {
      return MakeError(ENOENT, "rule doesn't exist");
    }

    return MakeError(0);
  }

  // Remove all rules from the table.
  void ClearRules() { table_.Clear(); }

  size_t Size() const { return table_.Count(); }

  // Extract an ExactMatchKey from `buf` based on the fields that have been
  // added to this table.
  ExactMatchKey MakeKey(const void *buf) const {
    ExactMatchKey key;
    DoMakeKeys(&key, &buf, 1);
    return key;
  }

  // Extract ExactMatchKeys from `batch` into `keys` based on the fields
  // that have been added to this table. Note that this function will call
  // `buffer_fn` `num_fields_` * `batch->size()` times, since certain fields may
  // be based on metadata attributes. If you know your workload will never need
  // to match on metadata attributes, consider manually creating buffers from
  // your PacketBatches and instead use
  // `MakeKeys(const void**, ExactMatchKey *, size_t)`
  template <typename BufferFunc>
  void MakeKeys(const PacketBatch *batch, const BufferFunc &buffer_fn,
                ExactMatchKey *keys) const {
    size_t n = batch->cnt();
    // Initialize the padding with zero.  NB: if total_key_size_ is 0,
    // this is (-1 / 8) which since C++11 is defined to be 0.  If
    // total_key_size_ == raw_key_size_, this is unnecessary, but
    // harmless.
    size_t last = (total_key_size_ - 1) / 8;
    for (size_t i = 0; i < n; i++) {
      keys[i].u64_arr[last] = 0;
    }
    for (size_t i = 0; i < num_fields_; i++) {
      uint64_t mask = fields_[i].mask;
      int pos = fields_[i].pos;

      for (size_t j = 0; j < n; j++) {
        uint8_t *k = reinterpret_cast<uint8_t *>(keys[j].u64_arr) + pos;
        *(reinterpret_cast<uint64_t *>(k)) =
            *reinterpret_cast<const uint64_t *>(
                buffer_fn(batch->pkts()[j], fields_[i])) &
            mask;
      }
    }
  }

  // Extract `n` ExactMatchKeys from `bufs` into `keys` based on the fields that
  // have been added to this table.
  void MakeKeys(const void **bufs, ExactMatchKey *keys, size_t n) const {
    DoMakeKeys(keys, bufs, n);
  }

  // Find an entry in the table.
  // Returns the value if `key` matches a rule, otherwise `default_value`.
  T Find(const ExactMatchKey &key, const T &default_value) const {
    const auto &table = table_;
    const auto *entry = table.Find(key, ExactMatchKeyHash(total_key_size_),
                                   ExactMatchKeyEq(total_key_size_));
    return entry ? entry->second : default_value;
  }

  // Find entries for `n` `keys` in the table and store their values in in
  // `vals`.  Keys without entries will have their corresponding entires in
  // `vals` set to `default_value`.
  void Find(const ExactMatchKey *keys, T *vals, size_t n,
            T default_value) const {
    const auto &table = table_;
    for (size_t i = 0; i < n; i++) {
      const auto *entry =
          table.Find(keys[i], ExactMatchKeyHash(total_key_size_),
                     ExactMatchKeyEq(total_key_size_));
      vals[i] = entry ? entry->second : default_value;
    }
  }

  uint32_t total_key_size() const { return total_key_size_; }

  // Set the `idx`th field of this table to one at offset `offset` bytes into a
  // buffer with length `size` and mask `mask`.
  // Returns 0 on success, non-zero errno on failure.
  Error AddField(int offset, int size, uint64_t mask, int idx) {
    ExactMatchField f = {
        .mask = mask, .attr_id = 0, .offset = offset, .pos = 0, .size = size};
    return DoAddField(f, "", idx, nullptr);
  }

  // Set the `idx`th field of this table to one at the offset of the
  // `mt_attr_name` metadata field as seen by module `m`, with length `size` and
  // mask `mask`.
  // Returns 0 on success, non-zero errno on failure.
  Error AddField(Module *m, const std::string &mt_attr_name, int size,
                 uint64_t mask, int idx) {
    ExactMatchField f = {
        .mask = mask, .attr_id = 0, .offset = 0, .pos = 0, .size = size};
    return DoAddField(f, mt_attr_name, idx, m);
  }

  size_t num_fields() const { return num_fields_; }

  // Returns the ith field.
  const ExactMatchField &get_field(size_t i) const { return fields_[i]; }

  typename EmTable::iterator begin() { return table_.begin(); }

  typename EmTable::iterator end() { return table_.end(); }

 private:
  Error MakeError(int code, const std::string &msg = "") {
    return std::make_pair(code, msg);
  }

  // Turn a rule into a key.
  // Returns 0 on success, non-zero errno on failure.
  Error gather_key(const ExactMatchRuleFields &fields, ExactMatchKey *key) {
    if (fields.size() != num_fields_) {
      return MakeError(EINVAL, Format("rule should have %zu fields (has %zu)",
                                      num_fields_, fields.size()));
    }

    *key = {};

    for (size_t i = 0; i < fields.size(); i++) {
      int field_size = fields_[i].size;
      int field_pos = fields_[i].pos;

      const std::vector<uint8_t> &f_obj = fields[i];

      if (static_cast<size_t>(field_size) != f_obj.size()) {
        return MakeError(
            EINVAL, Format("rule field %zu should have size %d (has %zu)", i,
                           field_size, f_obj.size()));
      }

      memcpy(reinterpret_cast<uint8_t *>(key) + field_pos, f_obj.data(),
             field_size);
    }

    return MakeError(0);
  }

  // Helper for public MakeKey functions
  void DoMakeKeys(ExactMatchKey *keys, const void **bufs, size_t n) const {
    // Initialize the padding with zero.  NB: if total_key_size_ is 0,
    // this is (-1 / 8) which since C++11 is defined to be 0.  If
    // total_key_size_ == raw_key_size_, this is unnecessary, but
    // harmless.
    size_t last = (total_key_size_ - 1) / 8;
    for (size_t i = 0; i < n; i++) {
      keys[i].u64_arr[last] = 0;
    }
    for (size_t i = 0; i < num_fields_; i++) {
      uint64_t mask = fields_[i].mask;
      int offset = fields_[i].offset;
      int pos = fields_[i].pos;

      for (size_t j = 0; j < n; j++) {
        uint8_t *k = reinterpret_cast<uint8_t *>(keys[j].u64_arr) + pos;

        *(reinterpret_cast<uint64_t *>(k)) =
            *(reinterpret_cast<const uint64_t *>(
                reinterpret_cast<const uint8_t *>(bufs[j]) + offset)) &
            mask;
      }
    }
  }

  // Helper for public AddField functions.
  // DoAddField inserts `field` as the `idx`th field for this table.
  // If `mt_attr_name` is set, the `offset` field of `field` will be ignored and
  // the inserted field will use the offset of `mt_attr_name` as reported by the
  // module `m`.
  // Returns 0 on success, non-zero errno on failure.
  Error DoAddField(const ExactMatchField &field,
                   const std::string &mt_attr_name, int idx,
                   Module *m = nullptr) {
    if (idx >= MAX_FIELDS) {
      return MakeError(EINVAL,
                       Format("idx %d is not in [0,%d)", idx, MAX_FIELDS));
    }
    ExactMatchField *f = &fields_[idx];
    f->size = field.size;
    if (f->size < 1 || f->size > MAX_FIELD_SIZE) {
      return MakeError(EINVAL, Format("idx %d: 'size' must be in [1,%d]", idx,
                                      MAX_FIELD_SIZE));
    }

    if (mt_attr_name.length() > 0) {
      f->attr_id = m->AddMetadataAttr(mt_attr_name, f->size,
                                      metadata::Attribute::AccessMode::kRead);
      if (f->attr_id < 0) {
        return MakeError(-f->attr_id,
                         Format("idx %d: add_metadata_attr() failed", idx));
      }
    } else {
      f->attr_id = -1;
      f->offset = field.offset;
      if (f->offset < 0 || f->offset > 1024) {
        return MakeError(EINVAL, Format("idx %d: invalid 'offset'", idx));
      }
    }

    int force_be = (f->attr_id < 0);

    if (field.mask == 0) {
      /* by default all bits are considered */
      f->mask = SetBitsHigh<uint64_t>(f->size * 8);
    } else {
      if (!utils::uint64_to_bin(&f->mask, field.mask, f->size,
                                utils::is_be_system() | force_be)) {
        return MakeError(
            EINVAL, Format("idx %d: not a valid %d-byte mask", idx, f->size));
      }
    }

    if (f->mask == 0) {
      return MakeError(EINVAL, Format("idx %d: empty mask", idx));
    }

    num_fields_++;
    f->pos = raw_key_size_;
    raw_key_size_ += f->size;
    total_key_size_ = align_ceil(raw_key_size_, sizeof(uint64_t));

    return MakeError(0);
  }

  // unaligend key size, used as an accumulator for calls to AddField()
  size_t raw_key_size_;

  // aligned total key size
  size_t total_key_size_;

  size_t num_fields_;
  ExactMatchField fields_[MAX_FIELDS];

  EmTable table_;
};

}  // namespace bess
}  // namespace utils

#endif  // BESS_UTILS_EXACT_MATCH_TABLE_H_
