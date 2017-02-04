/* Streamlined hash table implementation, with emphasis on lookup performance.
 * Key and value sizes are fixed. Lookup is thread-safe, but update is not. */

#ifndef BESS_UTILS_CUCKOOMAP_H_
#define BESS_UTILS_CUCKOOMAP_H_

#include <algorithm>
#include <functional>
#include <limits>
#include <stack>
#include <utility>
#include <vector>

#include <glog/logging.h>

#include "../mem_alloc.h"
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

  CuckooMap()
      : bucket_mask_(kInitNumBucket - 1),
        num_entries_(0),
        buckets_(kInitNumBucket),
        entries_(kInitNumEntries),
        free_entry_indices_() {
    for (int i = kInitNumEntries - 1; i >= 0; --i) {
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
    HashResult primary = Hash(key, hasher);

    Entry* entry = FindWithHash(primary, key, eq);
    if (entry) {
      entry->second = value;
      return entry;
    }

    HashResult secondary = HashSecondary(primary);

    while ((entry = AddEntry(primary, secondary, key, value, hasher)) ==
           nullptr) {
      // expand the table as the last resort
      ExpandBuckets();
    }
    return entry;
  }

  // Find the pointer to the stored value by the key.
  // Return nullptr if not exist.
  Entry* Find(const K& key, const H& hasher = H(), const E& eq = E()) {
    return FindWithHash(Hash(key, hasher), key, eq);
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
    int slot_idx = FindSlot(bucket, 0);

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

    int slot_idx = FindSlot(bucket, primary);
    if (slot_idx == -1) {
      return false;
    }

    EntryIndex idx = bucket.entry_indices[slot_idx];
    Entry& entry = entries_[idx];
    if (Eq(entry.first, key, eq)) {
      bucket.hash_values[slot_idx] = 0;
      entry = Entry();
      PushFreeEntryIndex(idx);
      num_entries_--;
      return true;
    }

    return false;
  }

  // Remove key from the bucket indexed by bucket_idx
  // Return the pointer to the entry if success. Otherwise return nullptr.
  Entry* GetFromBucket(HashResult primary, HashResult bucket_idx, const K& key,
                       const E& eq) {
    const Bucket& bucket = buckets_[bucket_idx];

    int slot_idx = FindSlot(bucket, primary);
    if (slot_idx == -1) {
      return nullptr;
    }

    EntryIndex idx = bucket.entry_indices[slot_idx];
    if (Eq(entries_[idx].first, key, eq)) {
      return &entries_[idx];
    }

    return nullptr;
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

  // Return the slot index in the bucket that matches the hash_value
  // -1 if not found.
  int FindSlot(const Bucket& bucket, HashResult hash_value) const {
    for (int i = 0; i < kEntriesPerBucket; i++) {
      if (bucket.hash_values[i] == hash_value) {
        return i;
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

      // Find empty slot
      int j = FindSlot(buckets_[alt_index], 0);
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
  Entry* FindWithHash(HashResult primary, const K& key, const E& eq) {
    Entry* ret = GetFromBucket(primary, primary & bucket_mask_, key, eq);
    if (ret) {
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

  // Primary hash value
  HashResult Hash(const K& key, const H& hasher) const {
    return hasher(key) | (1u << 31);
  }

  bool Eq(const K& lhs, const K& rhs, const E& eq) const {
    return eq(lhs, rhs);
  }

  // Resize the space of entries. Grow less aggressively than buckets.
  void ExpandEntries() {
    size_t old_size = num_entries_;
    size_t new_size = old_size + old_size / 2;

    entries_.resize(new_size);

    for (EntryIndex i = new_size - 1; i >= old_size; --i) {
      free_entry_indices_.push(i);
    }
  }

  // Resize the space of buckets.
  void ExpandBuckets() {
    size_t new_size = (bucket_mask_ + 1) * 2;
    buckets_.resize(new_size);
    bucket_mask_ = new_size - 1;
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
