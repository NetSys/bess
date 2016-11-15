/* Streamlined hash table implementation, with emphasis on lookup performance.
 * Key and value sizes are fixed. Lookup is thread-safe, but update is not. */

#ifndef BESS_UTILS_HTABLE_H_
#define BESS_UTILS_HTABLE_H_

#include <algorithm>
#include <cstring>

#include "common.h"

class HTableBase {
 public:
  /* compatible with DPDK's */
  typedef uint32_t (*HashFunc)(const void *key, uint32_t key_len,
                               uint32_t init_val);

  /* if the keys are identical, should return 0 */
  typedef int (*KeyCmpFunc)(const void *key, const void *key_stored,
                            size_t key_size);

  static const KeyCmpFunc kDefaultKeyCmpFunc;
  static const HashFunc kDefaultHashFunc;

  struct ht_params {
    size_t key_size;
    size_t value_size;
    size_t key_align;
    size_t value_align;

    uint32_t num_buckets; /* must be a power of 2 */
    int num_entries;      /* >= 4 */

    HashFunc hash_func;
    KeyCmpFunc keycmp_func;
  };

  HTableBase()
      : key_size_(),
        value_size_(),
        value_offset_(),
        entry_size_(),
        bucket_mask_(),
        buckets_(),
        entries_(),
        cnt_(),
        num_entries_(),
        free_keyidx_(),
        hash_func_(),
        keycmp_func_() {}

  virtual ~HTableBase() { Close(); };

  // not allowing copying for now
  HTableBase(HTableBase &) = delete;
  HTableBase(HTableBase &&) = delete;

  HTableBase &operator=(HTableBase &) = default;
  HTableBase &operator=(HTableBase &&) = default;

  /* -errno, or 0 for success */
  int Init(size_t key_size, size_t value_size);
  int InitEx(struct ht_params *params);
  void Close();

  void Clear();

  /* returns nullptr or the pointer to the data */
  void *Get(const void *key) const;

  /* identical to Get(), but you can supply a precomputed hash value "pri" */
  void *GetHash(uint32_t pri, const void *key) const;

  /* -ENOMEM on error, 0 for succesful insertion, or 1 if updated */
  int Set(const void *key, const void *value);

  /* -ENOENT on error, or 0 for success */
  int Del(const void *key);

  /* Iterate over key pointers.
   * nullptr if it reached the end of the table, or the pointer to the key.
   * User should set *next to 0 when starting iteration */
  void *Iterate(uint32_t *next) const;

  int Count() const;

  /* with non-zero 'detail', each item in the hash table will be shown */
  void Dump(bool detail) const;

 protected:
  typedef uint32_t KeyIndex;

  static const int kEntriesPerBucket = 4; /* 4-way set associative */

  struct alignas(32) Bucket {
    uint32_t hv[kEntriesPerBucket];
    KeyIndex keyidx[kEntriesPerBucket];
  };

  static uint32_t make_nonzero(uint32_t v) {
    /* Set the MSB and unset the 2nd MSB (NOTE: must be idempotent).
     * Then the result will never be a zero, and not a non-number when
     * represented as float (so we are good to use _mm_*_ps() SIMD ops) */
    return (v | (1u << 31)) & (~(1u << 30));
  }

  static uint32_t hash_secondary(uint32_t primary) {
    /* from DPDK */
    uint32_t tag = primary >> 12;
    return primary ^ ((tag + 1) * 0x5bd1e995);
  }

  void *keyidx_to_ptr(KeyIndex idx) const {
    return (void *)((uintptr_t)entries_ + entry_size_ * idx);
  }

  Bucket *hv_to_bucket(uint32_t hv) const {
    return &buckets_[hv & bucket_mask_];
  }

  /* in bytes */
  size_t key_size_;
  size_t value_size_;
  size_t value_offset_;
  size_t entry_size_;

 private:
  /* tunable macros */
  static const int kInitNumBucket = 4;
  static const int kInitNumEntries = 16;

  /* 4^kMaxCuckooPath buckets will be considered to make a empty slot,
   * before giving up and expand the table.
   * Higher number will yield better occupancy, but the worst case performance
   * of insertion will grow exponentially, so be careful. */
  static const int kMaxCuckooPath = 3;

  /* non-tunable macros */
  static const uint32_t kHashInitval = UINT32_MAX;
  static const KeyIndex kInvalidKeyIdx = std::numeric_limits<KeyIndex>::max();

  int count_entries_in_pri_bucket() const;
  void *get_from_bucket(uint32_t pri, uint32_t hv, const void *key) const;
  int del_from_bucket(uint32_t pri, uint32_t hv, const void *key);
  int add_entry(uint32_t pri, uint32_t sec, const void *key, const void *value);
  int add_to_bucket(Bucket *bucket, const void *key, const void *value);
  int find_empty_slot(const Bucket *bucket);
  int make_space(Bucket *bucket, int depth);
  KeyIndex pop_free_keyidx();
  // XXX: clone_table() is a good candidate for a copy constructor
  int clone_table(HTableBase *t_old, uint32_t num_buckets,
                  KeyIndex num_entries);
  int expand_buckets();
  int expand_entries();
  void push_free_keyidx(KeyIndex idx);
  uint32_t hash(const void *key) const;
  uint32_t hash_nonzero(const void *key) const;

  KeyIndex get_next(KeyIndex curr) const;

  void *key_to_value(const void *key) const;
  KeyIndex _get_keyidx(uint32_t pri) const;

  /* # of buckets == mask + 1 */
  uint32_t bucket_mask_;

  /* bucket and entry arrays grow independently */
  Bucket *buckets_;
  void *entries_;   /* entry_size * num_entries bytes */

  int cnt_;              /* current number of entries */
  KeyIndex num_entries_; /* current array size (# entries) */

  /* Linked list head for empty key slots (LIFO). kInvalidKeyIdx if empty */
  KeyIndex free_keyidx_;

  HashFunc hash_func_;
  KeyCmpFunc keycmp_func_;
};

template <typename K, typename V,
          HTableBase::KeyCmpFunc C = HTableBase::kDefaultKeyCmpFunc,
          HTableBase::HashFunc H = HTableBase::kDefaultHashFunc>
class HTable : public HTableBase {
 public:
  /* returns nullptr or the pointer to the data */
  V *Get(const K *key) const;

  /* identical to ht_Get(), but you can supply a precomputed hash value "pri" */
  V *GetHash(uint32_t pri, const K *key) const;

 private:
  V *get_from_bucket(uint32_t pri, uint32_t hv, const K *key) const;
};

template <typename K, typename V, HTableBase::KeyCmpFunc C,
          HTableBase::HashFunc H>
inline V *HTable<K, V, C, H>::Get(const K *key) const {
  uint32_t pri = H(key, key_size_, kHashInitval);

  return GetHash(pri, key);
}

template <typename K, typename V, HTableBase::KeyCmpFunc C,
          HTableBase::HashFunc H>
inline V *HTable<K, V, C, H>::GetHash(uint32_t pri, const K *key) const {
  pri = make_nonzero(pri);

  /* check primary bucket */
  V *ret = get_from_bucket(pri, pri, key);
  if (ret) return ret;

  /* check secondary bucket */
  return get_from_bucket(pri, hash_secondary(pri), key);
}

template <typename K, typename V, HTableBase::KeyCmpFunc C,
          HTableBase::HashFunc H>
inline V *HTable<K, V, C, H>::get_from_bucket(uint32_t pri, uint32_t hv,
                                              const K *key) const {
  Bucket *bucket = hv_to_bucket(hv);

  for (int i = 0; i < kEntriesPerBucket; i++) {
    KeyIndex k_idx;
    void *key_stored;

    if (pri != bucket->hv[i]) continue;

    k_idx = bucket->keyidx[i];
    key_stored = keyidx_to_ptr(k_idx);

    if (C(key, key_stored, key_size_) == 0)
      return (V *)((uintptr_t)key_stored + value_offset_);
  }

  return nullptr;
}

#endif  // BESS_UTILS_HTABLE_H_
