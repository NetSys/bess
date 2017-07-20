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

// Streamlined hash table implementation, with emphasis on lookup performance.
// Key and value sizes are fixed. Lookup is thread-safe, but update is not.
//
// Note: If you want to use a custom hash function, it should be a reasonably
// good one. If more than 8 (2 * kEntriesPerBucket) key values collide with
// the same hash value, Insert() may fail returning nullptr.

#ifndef BESS_UTILS_CUCKOOMAP_H_
#define BESS_UTILS_CUCKOOMAP_H_

#include <algorithm>
#include <functional>
#include <limits>
#include <stack>
#include <type_traits>
#include <utility>
#include <vector>

#include <glog/logging.h>

#include "../debug.h"
#include "common.h"

namespace bess {
namespace utils {

typedef uint32_t HashResult;
typedef uint32_t EntryIndex;

// A Hash table implementation using cuckoo hashing
//
// Example usage:
//
//  CuckooMap<uint32_t, uint64_t> cuckoo;
//  cuckoo.Insert(1, 99);
//  std::pair<uint32_t, uint64_t>* result = cuckoo.Find(1)
//  std::cout << "key: " << result->first << ", value: "
//    << result->second << std::endl;
//
// The output should be "key: 1, value: 99"
//
// For more examples, please refer to cuckoo_map_test.cc

template <typename K, typename V, typename H = std::hash<K>,
          typename E = std::equal_to<K>>
class CuckooMap {
 public:
  typedef std::pair<K, V> Entry;
  class iterator {
   public:
    using difference_type = std::ptrdiff_t;
    using value_type = Entry;
    using pointer = Entry*;
    using reference = Entry&;
    using iterator_category = std::forward_iterator_tag;

    iterator(CuckooMap& map, size_t bucket, size_t slot)
        : map_(map), bucket_idx_(bucket), slot_idx_(slot) {
      while (bucket_idx_ < map_.buckets_.size() &&
             map_.buckets_[bucket_idx_].hash_values[slot_idx_] == 0) {
        slot_idx_++;
        if (slot_idx_ == kEntriesPerBucket) {
          slot_idx_ = 0;
          bucket_idx_++;
        }
      }
    }

    iterator& operator++() {  // Pre-increment
      do {
        slot_idx_++;
        if (slot_idx_ == kEntriesPerBucket) {
          slot_idx_ = 0;
          bucket_idx_++;
        }
      } while (bucket_idx_ < map_.buckets_.size() &&
               map_.buckets_[bucket_idx_].hash_values[slot_idx_] == 0);
      return *this;
    }

    iterator operator++(int) {  // Pre-increment
      iterator tmp(*this);
      do {
        slot_idx_++;
        if (slot_idx_ == kEntriesPerBucket) {
          slot_idx_ = 0;
          bucket_idx_++;
        }
      } while (bucket_idx_ < map_.buckets_.size() &&
               map_.buckets_[bucket_idx_].hash_values[slot_idx_] == 0);
      return tmp;
    }

    bool operator==(const iterator& rhs) const {
      return &map_ == &rhs.map_ && bucket_idx_ == rhs.bucket_idx_ &&
             slot_idx_ == rhs.slot_idx_;
    }

    bool operator!=(const iterator& rhs) const {
      return &map_ != &rhs.map_ || bucket_idx_ != rhs.bucket_idx_ ||
             slot_idx_ != rhs.slot_idx_;
    }

    reference operator*() {
      EntryIndex idx = map_.buckets_[bucket_idx_].entry_indices[slot_idx_];
      return map_.entries_[idx];
    }

    pointer operator->() {
      EntryIndex idx = map_.buckets_[bucket_idx_].entry_indices[slot_idx_];
      return &map_.entries_[idx];
    }

   private:
    CuckooMap& map_;
    size_t bucket_idx_;
    size_t slot_idx_;
  };

  CuckooMap(size_t reserve_buckets = kInitNumBucket,
            size_t reserve_entries = kInitNumEntries)
      : bucket_mask_(reserve_buckets - 1),
        num_entries_(0),
        buckets_(reserve_buckets),
        entries_(reserve_entries),
        free_entry_indices_() {
    // the number of buckets must be a power of 2
    CHECK_EQ(align_ceil_pow2(reserve_buckets), reserve_buckets);

    for (int i = reserve_entries - 1; i >= 0; --i) {
      free_entry_indices_.push(i);
    }
  }

  // Not allowing copying for now
  CuckooMap(CuckooMap&) = delete;
  CuckooMap& operator=(CuckooMap&) = delete;

  // Allow move
  CuckooMap(CuckooMap&&) = default;
  CuckooMap& operator=(CuckooMap&&) = default;

  iterator begin() { return iterator(*this, 0, 0); }
  iterator end() { return iterator(*this, buckets_.size(), 0); }

  // Insert/update a key value pair
  // Return the pointer to the inserted entry
  Entry* Insert(const K& key, const V& value, const H& hasher = H(),
                const E& eq = E()) {
    Entry* entry;
    HashResult primary = Hash(key, hasher);

    EntryIndex idx = FindWithHash(primary, key, eq);
    if (idx != kInvalidEntryIdx) {
      entry = &entries_[idx];
      entry->second = value;
      return entry;
    }

    HashResult secondary = HashSecondary(primary);

    int trials = 0;

    while ((entry = AddEntry(primary, secondary, key, value, hasher)) ==
           nullptr) {
      if (++trials >= 3) {
        LOG_FIRST_N(WARNING, 1)
            << "CuckooMap: Excessive hash colision detected:\n"
            << bess::debug::DumpStack();
        return nullptr;
      }

      // expand the table as the last resort
      ExpandBuckets(hasher, eq);
    }
    return entry;
  }

  // Find the pointer to the stored value by the key.
  // Return nullptr if not exist.
  Entry* Find(const K& key, const H& hasher = H(), const E& eq = E()) {
    // Blame Effective C++ for this
    return const_cast<Entry*>(
        static_cast<
            const typename std::remove_reference<decltype(*this)>::type&>(*this)
            .Find(key, hasher, eq));
  }

  // const version of Find()
  const Entry* Find(const K& key, const H& hasher = H(),
                    const E& eq = E()) const {
    EntryIndex idx = FindWithHash(Hash(key, hasher), key, eq);
    if (idx == kInvalidEntryIdx) {
      return nullptr;
    }

    const Entry* ret = &entries_[idx];
    promise(ret != nullptr);
    return ret;
  }

  // Remove the stored entry by the key
  // Return false if not exist.
  bool Remove(const K& key, const H& hasher = H(), const E& eq = E()) {
    HashResult pri = Hash(key, hasher);
    if (RemoveFromBucket(pri, pri & bucket_mask_, key, eq)) {
      return true;
    }
    HashResult sec = HashSecondary(pri);
    if (RemoveFromBucket(pri, sec & bucket_mask_, key, eq)) {
      return true;
    }
    return false;
  }

  void Clear() {
    buckets_.clear();
    entries_.clear();

    // std::stack doesn't have a clear() method. Strange.
    while (!free_entry_indices_.empty()) {
      free_entry_indices_.pop();
    }

    num_entries_ = 0;
    bucket_mask_ = kInitNumBucket - 1;
    buckets_.resize(kInitNumBucket);
    entries_.resize(kInitNumEntries);

    for (int i = kInitNumEntries - 1; i >= 0; --i) {
      free_entry_indices_.push(i);
    }
  }

  // Return the number of stored entries
  size_t Count() const { return num_entries_; }

 protected:
  // Tunable macros
  static const int kInitNumBucket = 4;
  static const int kInitNumEntries = 16;
  static const int kEntriesPerBucket = 4;  // 4-way set associative

  // 4^kMaxCuckooPath buckets will be considered to make a empty slot,
  // before giving up and expand the table.
  // Higher number will yield better occupancy, but the worst case performance
  // of insertion will grow exponentially, so be careful.
  static const int kMaxCuckooPath = 3;

  /* non-tunable macros */
  static const EntryIndex kInvalidEntryIdx =
      std::numeric_limits<EntryIndex>::max();

  struct Bucket {
    HashResult hash_values[kEntriesPerBucket];
    EntryIndex entry_indices[kEntriesPerBucket];

    Bucket() : hash_values(), entry_indices() {}
  };

  // Push an unused entry index back to the  stack
  void PushFreeEntryIndex(EntryIndex idx) { free_entry_indices_.push(idx); }

  // Pop a free entry index from stack and return the index
  EntryIndex PopFreeEntryIndex() {
    if (free_entry_indices_.empty()) {
      ExpandEntries();
    }
    size_t idx = free_entry_indices_.top();
    free_entry_indices_.pop();
    return idx;
  }

  // Try to add (key, value) to the bucket indexed by bucket_idx
  // Return the pointer to the entry if success. Otherwise return nullptr.
  Entry* AddToBucket(HashResult bucket_idx, const K& key, const V& value,
                     const H& hasher) {
    Bucket& bucket = buckets_[bucket_idx];
    int slot_idx = FindEmptySlot(bucket);
    if (slot_idx == -1) {
      return nullptr;
    }

    EntryIndex free_idx = PopFreeEntryIndex();

    bucket.hash_values[slot_idx] = Hash(key, hasher);
    bucket.entry_indices[slot_idx] = free_idx;

    Entry& entry = entries_[free_idx];
    entry.first = key;
    entry.second = value;

    num_entries_++;
    return &entry;
  }

  // Remove key from the bucket indexed by bucket_idx
  // Return true if success.
  bool RemoveFromBucket(HashResult primary, HashResult bucket_idx, const K& key,
                        const E& eq) {
    Bucket& bucket = buckets_[bucket_idx];

    int slot_idx = FindSlot(bucket, primary, key, eq);
    if (slot_idx == -1) {
      return false;
    }

    bucket.hash_values[slot_idx] = 0;

    EntryIndex idx = bucket.entry_indices[slot_idx];
    entries_[idx] = Entry();
    PushFreeEntryIndex(idx);

    num_entries_--;
    return true;
  }

  // Find key from the bucket indexed by bucket_idx
  // Return the index of the entry if success. Otherwise return nullptr.
  EntryIndex GetFromBucket(HashResult primary, HashResult bucket_idx,
                           const K& key, const E& eq) const {
    const Bucket& bucket = buckets_[bucket_idx];

    int slot_idx = FindSlot(bucket, primary, key, eq);
    if (slot_idx == -1) {
      return kInvalidEntryIdx;
    }

    // this promise gains 5% performance improvement
    EntryIndex idx = bucket.entry_indices[slot_idx];
    promise(idx != kInvalidEntryIdx);
    return idx;
  }

  // Try to add the entry (key, value)
  // Return the pointer to the entry if success. Otherwise return nullptr.
  Entry* AddEntry(HashResult primary, HashResult secondary, const K& key,
                  const V& value, const H& hasher) {
    HashResult primary_bucket_index, secondary_bucket_index;
    Entry* entry = nullptr;
  again:
    primary_bucket_index = primary & bucket_mask_;
    if ((entry = AddToBucket(primary_bucket_index, key, value, hasher)) !=
        nullptr) {
      return entry;
    }

    secondary_bucket_index = secondary & bucket_mask_;
    if ((entry = AddToBucket(secondary_bucket_index, key, value, hasher)) !=
        nullptr) {
      return entry;
    }

    if (MakeSpace(primary_bucket_index, 0, hasher) >= 0) {
      goto again;
    }

    if (MakeSpace(secondary_bucket_index, 0, hasher) >= 0) {
      goto again;
    }

    return nullptr;
  }

  // Return an empty slot index in the bucket
  int FindEmptySlot(const Bucket& bucket) const {
    for (int i = 0; i < kEntriesPerBucket; i++) {
      if (bucket.hash_values[i] == 0) {
        return i;
      }
    }
    return -1;
  }

  // Return the slot index in the bucket that matches the primary hash_value
  // and the actual key. Return -1 if not found.
  int FindSlot(const Bucket& bucket, HashResult primary, const K& key,
               const E& eq) const {
    for (int i = 0; i < kEntriesPerBucket; i++) {
      if (bucket.hash_values[i] == primary) {
        EntryIndex idx = bucket.entry_indices[i];
        const Entry& entry = entries_[idx];

        if (likely(Eq(entry.first, key, eq))) {
          return i;
        }
      }
    }
    return -1;
  }

  // Recursively try making an empty slot in the bucket
  // Returns a slot index in [0, kEntriesPerBucket) for successful operation,
  // or -1 if failed.
  int MakeSpace(HashResult index, int depth, const H& hasher) {
    if (depth >= kMaxCuckooPath) {
      return -1;
    }

    Bucket& bucket = buckets_[index];

    for (int i = 0; i < kEntriesPerBucket; i++) {
      EntryIndex idx = bucket.entry_indices[i];
      const K& key = entries_[idx].first;
      HashResult pri = Hash(key, hasher);
      HashResult sec = HashSecondary(pri);

      HashResult alt_index;

      // this entry is in its primary bucket?
      if (pri == bucket.hash_values[i]) {
        alt_index = sec & bucket_mask_;
      } else if (sec == bucket.hash_values[i]) {
        alt_index = pri & bucket_mask_;
      } else {
        return -1;
      }

      int j = FindEmptySlot(buckets_[alt_index]);
      if (j == -1) {
        j = MakeSpace(alt_index, depth + 1, hasher);
      }
      if (j >= 0) {
        Bucket& alt_bucket = buckets_[alt_index];
        alt_bucket.hash_values[j] = bucket.hash_values[i];
        alt_bucket.entry_indices[j] = bucket.entry_indices[i];
        bucket.hash_values[i] = 0;
        return i;
      }
    }

    return -1;
  }

  // Get the entry given the primary hash value of the key.
  // Returns the pointer to the entry or nullptr if failed.
  EntryIndex FindWithHash(HashResult primary, const K& key, const E& eq) const {
    EntryIndex ret = GetFromBucket(primary, primary & bucket_mask_, key, eq);
    if (ret != kInvalidEntryIdx) {
      return ret;
    }
    return GetFromBucket(primary, HashSecondary(primary) & bucket_mask_, key,
                         eq);
  }

  // Secondary hash value
  static HashResult HashSecondary(HashResult primary) {
    HashResult tag = primary >> 12;
    return primary ^ ((tag + 1) * 0x5bd1e995);
  }

  // Primary hash value. Should always be non-zero (= not empty)
  static HashResult Hash(const K& key, const H& hasher) {
    return hasher(key) | (1u << 31);
  }

  static bool Eq(const K& lhs, const K& rhs, const E& eq) {
    return eq(lhs, rhs);
  }

  // Resize the space of entries. Grow less aggressively than buckets.
  void ExpandEntries() {
    size_t old_size = entries_.size();
    size_t new_size = old_size + old_size / 2;

    entries_.resize(new_size);

    for (EntryIndex i = new_size - 1; i >= old_size; --i) {
      free_entry_indices_.push(i);
    }
  }

  // Resize the space of buckets, and rehash existing entries
  void ExpandBuckets(const H& hasher, const E& eq) {
    CuckooMap<K, V, H, E> bigger(buckets_.size() * 2, entries_.size());

    for (const auto& e : *this) {
      // While very unlikely, this insert() may cause recursive expansion
      bool ret = bigger.Insert(e.first, e.second, hasher, eq);
      if (!ret) {
        return;
      }
    }

    bucket_mask_ = std::move(bigger.bucket_mask_);
    num_entries_ = bigger.num_entries_;
    buckets_ = std::move(bigger.buckets_);
    entries_ = std::move(bigger.entries_);
    free_entry_indices_ = std::move(bigger.free_entry_indices_);
  }

  // # of buckets == mask + 1
  HashResult bucket_mask_;

  // # of entries
  size_t num_entries_;

  // bucket and entry arrays grow independently
  std::vector<Bucket> buckets_;
  std::vector<Entry> entries_;

  // Stack of free entries
  std::stack<EntryIndex> free_entry_indices_;
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_CUCKOOMAP_H_
