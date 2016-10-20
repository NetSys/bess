/* Streamlined hash table implementation, with emphasis on lookup performance.
 * Key and value sizes are fixed. Lookup is thread-safe, but update is not. */

#ifndef _HTABLE_H_
#define _HTABLE_H_

#include <algorithm>
#include <cstring>

#include <rte_config.h>
#include <rte_hash_crc.h>

#include "../common.h"
#include "simd.h"

class HTableBase {
 public:
  /* compatible with DPDK's */
  typedef uint32_t (*HashFunc)(const void *key, uint32_t key_len,
                               uint32_t init_val);

  /* if the keys are identical, should return 0 */
  typedef int (*KeyCmpFunc)(const void *key, const void *key_stored,
                            size_t key_size);

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

  HTableBase() = default;
  ~HTableBase() { Close(); };

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

  /* returns NULL or the pointer to the data */
  void *Get(const void *key) const;

  /* identical to Get(), but you can supply a precomputed hash value "pri" */
  void *GetHash(uint32_t pri, const void *key) const;

  /* -ENOMEM on error, 0 for succesful insertion, or 1 if updated */
  int Set(const void *key, const void *value);

  /* -ENOENT on error, or 0 for success */
  int Del(const void *key);

  /* Iterate over key pointers.
   * NULL if it reached the end of the table, or the pointer to the key.
   * User should set *next to 0 when starting iteration */
  void *Iterate(uint32_t *next) const;

  int Count() const;

  /* with non-zero 'detail', each item in the hash table will be shown */
  void Dump(int detail) const;

 protected:
  typedef int32_t KeyIndex;

  /* tunable macros */
  static const int kInitNumBucket = 4;
  static const int kInitNumEntries = 16;

  /* 4^kMaxCuckooPath buckets will be considered to make a empty slot,
   * before giving up and expand the table.
   * Higher number will yield better occupancy, but the worst case performance
   * of insertion will grow exponentially, so be careful. */
  static const int kMaxCuckooPath = 3;

  /* non-tunable macros */
  static const int kEntriesPerBucket = 4; /* 4-way set associative */
  static const uint32_t kHashInitval = UINT32_MAX;
  static const KeyIndex kInvalidKeyIdx = INT32_MAX;
#define DEFAULT_HASH_FUNC rte_hash_crc

  struct Bucket {
    uint32_t hv[kEntriesPerBucket];
    KeyIndex keyidx[kEntriesPerBucket];
  } __ymm_aligned;

  static inline uint32_t make_nonzero(uint32_t v) {
    /* Set the MSB and unset the 2nd MSB (NOTE: must be idempotent).
     * Then the result will never be a zero, and not a non-number when
     * represented as float (so we are good to use _mm_*_ps() SIMD ops) */
    return (v | (1u << 31)) & (~(1u << 30));
  }

  static inline uint32_t hash_secondary(uint32_t primary) {
    /* from DPDK */
    uint32_t tag = primary >> 12;
    return primary ^ ((tag + 1) * 0x5bd1e995);
  }

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
  inline KeyIndex get_next(KeyIndex curr) const;
  inline void *keyidx_to_ptr(KeyIndex idx) const;
  inline uint32_t hash_nonzero(const void *key) const;
  inline uint32_t hash(const void *key) const;

  inline void *key_to_value(const void *key) const;
  inline KeyIndex _get_keyidx(uint32_t pri) const;

  /* bucket and entry arrays grow independently */
  Bucket *buckets_ = NULL;
  void *entries_ = NULL; /* entry_size * num_entries bytes */

  /* # of buckets == mask + 1 */
  uint32_t bucket_mask_ = {};

  int cnt_ = 0;              /* current number of entries */
  KeyIndex num_entries_ = 0; /* current array size (# entries) */

  /* Linked list head for empty key slots (LIFO). NO_NEXT if empty */
  KeyIndex free_keyidx_ = {};

  /* in bytes */
  size_t key_size_ = {};
  size_t value_size_ = {};
  size_t value_offset_ = {};
  size_t entry_size_ = {};

  HashFunc hash_func_;
  KeyCmpFunc keycmp_func_;
};

inline uint32_t HTableBase::hash(const void *key) const {
  return hash_func_(key, key_size_, kHashInitval);
}

inline uint32_t HTableBase::hash_nonzero(const void *key) const {
  return make_nonzero(hash(key));
}

inline void *HTableBase::keyidx_to_ptr(KeyIndex idx) const {
  return (void *)((uintptr_t)entries_ + entry_size_ * idx);
}

inline HTableBase::KeyIndex HTableBase::get_next(KeyIndex curr) const {
  return *(KeyIndex *)keyidx_to_ptr(curr);
}

template <typename K, typename V, HTableBase::KeyCmpFunc C = memcmp,
          HTableBase::HashFunc H = DEFAULT_HASH_FUNC>
class HTable : public HTableBase {
 public:
  /* returns NULL or the pointer to the data */
  V *Get(const K *key) const;
  void GetBulk(int num_keys, const K **keys, V **values) const;

  /* identical to ht_Get(), but you can supply a precomputed hash value "pri" */
  V *GetHash(uint32_t pri, const K *key) const;

 protected:
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
  uint32_t b_idx = hv & bucket_mask_;
  Bucket *bucket = &buckets_[b_idx];

  for (int i = 0; i < kEntriesPerBucket; i++) {
    KeyIndex k_idx;
    void *key_stored;

    if (pri != bucket->hv[i]) continue;

    k_idx = bucket->keyidx[i];
    key_stored = keyidx_to_ptr(k_idx);

    if (C(key, key_stored, key_size_) == 0)
      return (V *)((uintptr_t)key_stored + value_offset_);
  }

  return NULL;
}

#if __AVX__
template <typename K, typename V, HTableBase::KeyCmpFunc C,
          HTableBase::HashFunc H>
inline void HTable<K, V, C, H>::GetBulk(int num_keys, const K **keys,
                                        V **values) const {
  uint32_t bucket_mask = bucket_mask_;
  void *entries = entries_;
  size_t key_size = key_size_;
  size_t entry_size = entry_size_;
  size_t value_offset = value_offset_;

  for (int i = 0; i < num_keys; i++) {
    Bucket *pri_bucket;
    Bucket *sec_bucket;

    uint32_t pri = H(keys[i], key_size, kHashInitval);
    pri |= (1u << 31);
    pri &= ~(1u << 30);
    pri_bucket = &buckets_[pri & bucket_mask];

    uint32_t sec = hash_secondary(pri);
    sec_bucket = &buckets_[sec & bucket_mask];

    union {
      __m256i v;
      uint32_t a[8];
    } keyidx;

    __m256i v_pri = _mm256_set1_epi32(pri);
    __m256i v_pri_bucket = _mm256_load_si256((__m256i *)pri_bucket);
    __m256i v_sec_bucket = _mm256_load_si256((__m256i *)sec_bucket);
    __m256i v_hv = _mm256_permute2f128_si256(v_pri_bucket, v_sec_bucket, 0x20);
    keyidx.v = _mm256_permute2f128_si256(v_pri_bucket, v_sec_bucket, 0x31);

    __m256 v_cmp = _mm256_cmp_ps((__m256)v_pri, (__m256)v_hv, _CMP_EQ_OQ);

    int mask = _mm256_movemask_ps(v_cmp);
    int ffs = __builtin_ffs(mask);

    if (!ffs) {
      values[i] = NULL;
      continue;
    }

    KeyIndex k_idx = keyidx.a[ffs - 1];
    K *key_stored = (K *)((uintptr_t)entries + entry_size * k_idx);
    if (!C(keys[i], key_stored, key_size))
      values[i] = (V *)((uintptr_t)key_stored + value_offset);
    else
      values[i] = GetHash(pri, keys[i]);
  }
}
#else
template <typename K, typename V, HTableBase::KeyCmpFunc C,
          HTableBase::HashFunc H>
inline void HTable<K, V, C, H>::GetBulk(int num_keys, const K **keys,
                                        V **values) const {
  for (int i = 0; i < num_keys; i++)
    values[i] = Get(keys[i]);
}
#endif

#endif
