/* Streamlined hash table implementation, with emphasis on lookup performance.
 * Key and value sizes are fixed. Lookup is thread-safe, but update is not. */

#ifndef BESS_UTILS_CUCKOOMAP_H_
#define BESS_UTILS_CUCKOOMAP_H_

#include <algorithm>
#include <limits>
#include <stack>
#include <utility>
#include <vector>

#include <glog/logging.h>

#include "../mem_alloc.h"
#include "common.h"

namespace bess {
namespace utils {

// Hash function. Return size_t as the hash code.
template <typename K>
using HashFunc = size_t (*)(const K& key, size_t init_val);

// Compare function. Return true if the lhs and rhs are identical.
template <typename K>
using EqFunc = bool (*)(const K& lhs, const K& rhs);

template <typename K>
static inline size_t DefaultHashFunc(const K& key, size_t) {
  return std::hash<K>()(key);
}

template <typename K>
static inline bool DefaultEqFunc(const K& lhs, const K& rhs) {
  return lhs == rhs;
}

// Cuckoo HashMap implementation
template <typename K, typename V, HashFunc<K> H = DefaultHashFunc,
          EqFunc<K> E = DefaultEqFunc>
class CuckooMap {
 public:
  typedef std::pair<K, V> Entry;

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

  // Insert/update a key value pair
  // Return the pointer to the inserted entry
  Entry* Insert(const K& key, const V& value) {
    size_t primary = Hash(key);

    Entry* entry = GetHash(primary, key);
    if (entry) {
      entry->second = value;
      return entry;
    }

    size_t secondary = HashSecondary(primary);

    while ((entry = AddEntry(primary, secondary, key, value)) == nullptr) {
      // expand the table as the last resort
      ExpandBuckets();
    }
    return entry;
  }

  // Find the pointer to the stored value by the key.
  // Return nullptr if not exist.
  Entry* Find(const K& key) { return GetHash(Hash(key), key); }

  // Remove the stored entry by the key
  // Return false if not exist.
  bool Remove(const K& key) {
    size_t pri = Hash(key);
    if (RemoveFromBucket(pri, pri & bucket_mask_, key)) {
      return true;
    }
    size_t sec = HashSecondary(pri);
    if (RemoveFromBucket(pri, sec & bucket_mask_, key)) {
      return true;
    }
    return false;
  }

  // Return the number of stored entries
  size_t Count() const { return num_entries_; }

 private:
  // Tunable macros
  static const int kInitNumBucket = 4;
  static const int kInitNumEntries = 16;
  static const int kEntriesPerBucket = 4;  // 4-way set associative

  // 4^kMaxCuckooPath buckets will be considered to make a empty slot,
  // before giving up and expand the table.
  // Higher number will yield better occupancy, but the worst case performance
  // of insertion will grow exponentially, so be careful.
  static const int kMaxCuckooPath = 3;

  // non-tunable macros
  static const size_t kHashInitval = UINT64_MAX;

  struct Bucket {
    size_t hash_values[kEntriesPerBucket];
    size_t key_indices[kEntriesPerBucket];

    Bucket() : hash_values(), key_indices() {}
  };

  // Push an unused entry index back to the  stack
  void PushFreeKeyIndex(size_t idx) { free_entry_indices_.push(idx); }

  // Pop a free entry index from stack and return the index
  size_t PopFreeKeyIndex() {
    if (free_entry_indices_.empty()) {
      ExpandEntries();
    }
    size_t idx = free_entry_indices_.top();
    free_entry_indices_.pop();
    return idx;
  }

  // Try to add (key, value) to the bucket indexed by bucket_idx
  // Return the pointer to the entry if success. Otherwise return nullptr.
  Entry* AddToBucket(size_t bucket_idx, const K& key, const V& value) {
    Bucket& bucket = buckets_[bucket_idx];
    int slot_idx = FindSlot(bucket, 0);

    if (slot_idx == -1) {
      return nullptr;
    }

    size_t free_idx = PopFreeKeyIndex();

    bucket.hash_values[slot_idx] = Hash(key);
    bucket.key_indices[slot_idx] = free_idx;

    Entry& entry = entries_[free_idx];
    entry.first = key;
    entry.second = value;

    num_entries_++;
    return &entry;
  }

  // Remove key from the bucket indexed by bucket_idx
  // Return true if success.
  bool RemoveFromBucket(size_t primary, size_t bucket_idx, const K& key) {
    Bucket& bucket = buckets_[bucket_idx];

    int slot_idx = FindSlot(bucket, primary);
    if (slot_idx == -1) {
      return false;
    }

    size_t idx = bucket.key_indices[slot_idx];
    Entry& entry = entries_[idx];
    if (E(entry.first, key)) {
      bucket.hash_values[slot_idx] = 0;
      entry = Entry();
      PushFreeKeyIndex(idx);
      num_entries_--;
      return true;
    }

    return false;
  }

  // Remove key from the bucket indexed by bucket_idx
  // Return the pointer to the entry if success. Otherwise return nullptr.
  Entry* GetFromBucket(size_t primary, size_t bucket_idx, const K& key) {
    Bucket& bucket = buckets_[bucket_idx];

    int slot_idx = FindSlot(bucket, primary);
    if (slot_idx == -1) {
      return nullptr;
    }

    size_t idx = bucket.key_indices[slot_idx];
    if (E(entries_[idx].first, key)) {
      return &entries_[idx];
    }

    return nullptr;
  }

  // Try to add the entry (key, value)
  // Return the pointer to the entry if success. Otherwise return nullptr.
  Entry* AddEntry(size_t primary, size_t secondary, const K& key,
                  const V& value) {
    uint32_t primary_bucket_index, secondary_bucket_index;
    Entry* entry = nullptr;
  again:
    primary_bucket_index = primary & bucket_mask_;
    if ((entry = AddToBucket(primary_bucket_index, key, value)) != nullptr) {
      return entry;
    }

    secondary_bucket_index = secondary & bucket_mask_;
    if ((entry = AddToBucket(secondary_bucket_index, key, value)) != nullptr) {
      return entry;
    }

    if (MakeSpace(primary_bucket_index, 0) >= 0) {
      goto again;
    }

    if (MakeSpace(secondary_bucket_index, 0) >= 0) {
      goto again;
    }

    return nullptr;
  }

  // Return the slot index in the bucket that matches the hash_value
  // -1 if not found.
  int FindSlot(const Bucket& bucket, size_t hash_value) {
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
  int MakeSpace(size_t index, int depth) {
    if (depth >= kMaxCuckooPath) {
      return -1;
    }

    Bucket& bucket = buckets_[index];

    for (int i = 0; i < kEntriesPerBucket; i++) {
      const K& key = bucket.key_indices[i];
      size_t pri = Hash(key);
      size_t sec = HashSecondary(pri);

      size_t alt_index;

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
        j = MakeSpace(alt_index, depth + 1);
      }
      if (j >= 0) {
        Bucket& alt_bucket = buckets_[alt_index];
        alt_bucket.hash_values[j] = bucket.hash_values[i];
        alt_bucket.key_indices[j] = bucket.key_indices[i];
        bucket.hash_values[i] = 0;
        return i;
      }
    }

    return -1;
  }

  // Get the entry given the primary hash value of the key.
  // Returns the pointer to the entry or nullptr if failed.
  Entry* GetHash(size_t primary, const K& key) {
    Entry* ret = GetFromBucket(primary, primary & bucket_mask_, key);
    if (ret) {
      return ret;
    }
    return GetFromBucket(primary, HashSecondary(primary) & bucket_mask_, key);
  }

  // Secondary hash value
  static size_t HashSecondary(uint32_t primary) {
    size_t tag = primary >> 12;
    return primary ^ ((tag + 1) * 0x5bd1e995);
  }

  // Primary hash value
  static size_t Hash(const K& key) { return H(key, kHashInitval); }

  // Resize the space of entries. Grow less aggressively than buckets.
  void ExpandEntries() {
    size_t old_size = num_entries_;
    size_t new_size = old_size + old_size / 2;

    entries_.resize(new_size);

    for (size_t i = new_size - 1; i >= old_size; --i) {
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
  size_t bucket_mask_;

  // # of entries
  size_t num_entries_;

  // bucket and entry arrays grow independently
  std::vector<Bucket> buckets_;
  std::vector<Entry> entries_;

  // Stack of free entries
  std::stack<size_t> free_entry_indices_;
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_CUCKOOMAP_H_
