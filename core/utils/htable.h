/* Streamlined hash table implementation, with emphasis on lookup performance.
 * Key and value sizes are fixed. Lookup is thread-safe, but update is not. */

#ifndef _HTABLE_H_
#define _HTABLE_H_

#include <functional>
#include <stdint.h>

#include <rte_config.h>
#include <rte_hash_crc.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <rte_config.h>
#include <rte_hash_crc.h>

#include "../common.h"
#include "../mem_alloc.h"

#include "simd.h"

/* tunable macros */
#define INIT_NUM_BUCKETS 4
#define INIT_NUM_ENTRIES 16

/* 4^MAX_CUCKOO_PATH buckets will be considered to make a empty slot,
 * before giving up and expand the table.
 * Higher number will yield better occupancy, but the worst case performance
 * of insertion will grow exponentially, so be careful. */
#define MAX_CUCKOO_PATH 3

/* non-tunable macros */
#define ENTRIES_PER_BUCKET 4 /* 4-way set associative */

#define DEFAULT_HASH_INITVAL UINT32_MAX

#define DEFAULT_HASH_FUNC rte_hash_crc

/* from DPDK */
static inline uint32_t ht_hash_secondary(uint32_t primary) {
  uint32_t tag = primary >> 12;

  return primary ^ ((tag + 1) * 0x5bd1e995);
}

typedef int32_t ht_keyidx_t;

/* compatible with DPDK's */
typedef uint32_t (*ht_hash_func_t)(const void *key, uint32_t key_len,
                                   uint32_t init_val);

/* if the keys are identical, should return 0 */
typedef int (*ht_keycmp_func_t)(const void *key, const void *key_stored,
                                size_t key_size);

struct ht_params {
  size_t key_size;
  size_t value_size;
  size_t key_align;
  size_t value_align;

  uint32_t num_buckets; /* must be a power of 2 */
  int num_entries;      /* >= 4 */

  ht_hash_func_t hash_func;
  ht_keycmp_func_t keycmp_func;
};

struct ht_bucket {
  uint32_t hv[ENTRIES_PER_BUCKET];
  ht_keyidx_t keyidx[ENTRIES_PER_BUCKET];
} __ymm_aligned;

template <class K, class V, ht_keycmp_func_t C = memcmp,
          ht_hash_func_t H = DEFAULT_HASH_FUNC>
class HTable {
 public:
  HTable() = default;

  ~HTable() { Close(); };

  /* -errno, or 0 for success */
  int Init(size_t key_size, size_t value_size);
  int InitEx(struct ht_params *params);

  void Close();
  void Clear();

  /* returns NULL or the pointer to the data */
  V *Get(const K *key) const;
  inline void GetBulk(int num_keys, const K **_keys, V **values) const;

  /* identical to ht_Get(), but you can supply a precomputed hash value "pri" */
  V *GetHash(uint32_t pri, const K *key) const;

  /* -ENOMEM on error, 0 for succesful insertion, or 1 if updated */
  int Set(const K *key, const V *value);

  /* -ENOENT on error, or 0 for success */
  int Del(const K *key);

  /* Iterate over key pointers.
   * NULL if it reached the end of the table, or the pointer to the key.
   * User should set *next to 0 when starting iteration */
  K *Iterate(uint32_t *next) const;

  int Count() const;

  /* with non-zero 'detail', each item in the hash table will be shown */
  void Dump(int detail) const;

 private:
  int count_entries_in_pri_bucket() const;
  V *get_from_bucket(uint32_t pri, uint32_t hv, const K *key) const;
  int del_from_bucket(uint32_t pri, uint32_t hv, const K *key);
  int add_entry(uint32_t pri, uint32_t sec, const K *key, const V *value);
  int add_to_bucket(struct ht_bucket *bucket, const K *key, const V *value);
  int make_space(struct ht_bucket *bucket, int depth);
  ht_keyidx_t pop_free_keyidx();
  // XXX: clone_table() is a good candidate for a copy constructor
  int clone_table(HTable<K, V, C, H> *t_old, uint32_t num_buckets,
                  ht_keyidx_t num_entries);
  int expand_buckets();
  int expand_entries();
  void push_free_keyidx(ht_keyidx_t idx);
  inline ht_keyidx_t get_next(ht_keyidx_t curr) const;
  inline K *keyidx_to_ptr(ht_keyidx_t idx) const;
  inline uint32_t hash_nonzero(const K *key) const;
  inline uint32_t hash(const K *key) const;

  inline V *key_to_value(const K *key) const;
  inline ht_keyidx_t _get_keyidx(uint32_t pri) const;
#if __AVX__
  inline uint32_t _get_keyidx_vec(uint32_t pri) const;
#endif

  /* bucket and entry arrays grow independently */
  struct ht_bucket *buckets_ = NULL;
  void *entries_ = NULL; /* entry_size * num_entries bytes */

  /* # of buckets == mask + 1 */
  uint32_t bucket_mask_ = {};

  int cnt_ = 0;                 /* current number of entries */
  ht_keyidx_t num_entries_ = 0; /* current array size (# entries) */

  /* Linked list head for empty key slots (LIFO). NO_NEXT if empty */
  ht_keyidx_t free_keyidx_ = {};

  /* in bytes */
  size_t key_size_ = {};
  size_t value_size_ = {};
  size_t value_offset_ = {};
  size_t entry_size_ = {};

 public:
  // not allowing copying for now
  HTable(HTable &) = delete;
  HTable(HTable &&) = delete;

  HTable &operator=(HTable &) = default;
  HTable &operator=(HTable &&) = default;
};

/* from the stored key pointer, return its value pointer */
template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
inline V *HTable<K, V, C, H>::key_to_value(const K *key) const {
  return (V *)((char *)key + value_offset_);
}

#define INVALID_KEYIDX INT32_MAX

/* actually works faster for very small tables */
template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
inline ht_keyidx_t HTable<K, V, C, H>::_get_keyidx(uint32_t pri) const {
  struct ht_bucket *bucket = &buckets_[pri & bucket_mask_];

  for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
    if (pri == bucket->hv[i]) return bucket->keyidx[i];
  }

  uint32_t sec = ht_hash_secondary(pri);
  bucket = &buckets_[sec & bucket_mask_];
  for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
    if (pri == bucket->hv[i]) return bucket->keyidx[i];
  }

  return INVALID_KEYIDX;
}

#if __AVX__
template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
inline uint32_t HTable<K, V, C, H>::_get_keyidx_vec(uint32_t pri) const {
  struct ht_bucket *bucket = &buckets_[pri & bucket_mask_];

  __m128i v_pri = _mm_set1_epi32(pri);
  __m128i v_hv = _mm_load_si128((__m128i *)bucket->hv);
  __m128i v_cmp = _mm_cmpeq_epi32(v_hv, v_pri);
  int mask = _mm_movemask_epi8(v_cmp);
  int ffs = __builtin_ffs(mask);

  if (ffs > 0) return bucket->keyidx[ffs >> 2];

  uint32_t sec = ht_hash_secondary(pri);
  bucket = &buckets_[sec & bucket_mask_];

  v_hv = _mm_load_si128((__m128i *)bucket->hv);
  v_cmp = _mm_cmpeq_epi32(v_hv, v_pri);
  mask = _mm_movemask_epi8(v_cmp);
  ffs = __builtin_ffs(mask);

  if (ffs > 0) return bucket->keyidx[ffs >> 2];

  return INVALID_KEYIDX;
}
#else
#define _get_keyidx_vec _get_keyidx
#endif

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
inline void HTable<K, V, C, H>::GetBulk(int num_keys, const K **_keys,
                                        V **values) const {
  const K **keys = (const K **)_keys;
  uint32_t bucket_mask = bucket_mask_;
  void *entries = entries_;
  size_t key_size = key_size_;
  size_t entry_size = entry_size_;
  size_t value_offset = value_offset_;

  for (int i = 0; i < num_keys; i++) {
    struct ht_bucket *pri_bucket;
    struct ht_bucket *sec_bucket;

    uint32_t pri = H(keys[i], key_size, DEFAULT_HASH_INITVAL);
    pri |= (1u << 31);
    pri &= ~(1u << 30);
    pri_bucket = &buckets_[pri & bucket_mask];

    uint32_t sec = ht_hash_secondary(pri);
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

    ht_keyidx_t k_idx = keyidx.a[ffs - 1];
    K *key_stored = (K *)((uintptr_t)entries + entry_size * k_idx);
    if (!C(keys[i], key_stored, key_size))
      values[i] = (V *)((uintptr_t)key_stored + value_offset);
    else
      values[i] = GetHash(pri, keys[i]);
  }
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
inline uint32_t HTable<K, V, C, H>::hash(const K *key) const {
  return H(key, key_size_, UINT32_MAX);
}

static inline uint32_t ht_make_nonzero(uint32_t v) {
  /* Set the MSB and unset the 2nd MSB (NOTE: must be idempotent).
   * Then the result will never be a zero, and not a non-number when
   * represented as float (so we are good to use _mm_*_ps() SIMD ops) */
  return (v | (1u << 31)) & (~(1u << 30));
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
inline uint32_t HTable<K, V, C, H>::hash_nonzero(const K *key) const {
  return ht_make_nonzero(hash(key));
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
inline K *HTable<K, V, C, H>::keyidx_to_ptr(ht_keyidx_t idx) const {
  return (K *)((uintptr_t)entries_ + entry_size_ * idx);
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
inline ht_keyidx_t HTable<K, V, C, H>::get_next(ht_keyidx_t curr) const {
  return *(ht_keyidx_t *)keyidx_to_ptr(curr);
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
void HTable<K, V, C, H>::push_free_keyidx(ht_keyidx_t idx) {
  assert(0 <= idx && idx < num_entries_);

  *(ht_keyidx_t *)((uintptr_t)entries_ + entry_size_ * idx) = free_keyidx_;
  free_keyidx_ = idx;
}

/* entry array grows much more gently (50%) than bucket array (100%),
 * since space efficiency may be important for large keys and/or values. */
template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::expand_entries() {
  ht_keyidx_t old_size = num_entries_;
  ht_keyidx_t new_size = old_size + old_size / 2;

  void *new_entries;

  new_entries = mem_realloc(entries_, new_size * entry_size_);
  if (!new_entries) return -ENOMEM;

  num_entries_ = new_size;
  entries_ = new_entries;

  for (ht_keyidx_t i = new_size - 1; i >= old_size; i--) push_free_keyidx(i);

  return 0;
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
ht_keyidx_t HTable<K, V, C, H>::pop_free_keyidx() {
  ht_keyidx_t ret = free_keyidx_;

  if (ret == INVALID_KEYIDX) {
    ret = expand_entries();
    if (ret) return ret;

    ret = free_keyidx_;
  }

  free_keyidx_ = get_next(ret);

  return ret;
}

/* returns an empty slot ID, or -ENOSPC */
static int find_empty_slot(const struct ht_bucket *bucket) {
  for (int i = 0; i < ENTRIES_PER_BUCKET; i++)
    if (bucket->hv[i] == 0) return i;

  return -ENOSPC;
}

/* Recursive function to try making an empty slot in the bucket.
 * Returns a slot ID in [0, ENTRIES_PER_BUCKET) for successful operation,
 * or -ENOSPC if failed */
template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::make_space(struct ht_bucket *bucket, int depth) {
  if (depth >= MAX_CUCKOO_PATH) return -ENOSPC;

  /* Something is wrong if there's already an empty slot in this bucket */
  assert(find_empty_slot(bucket) == -ENOSPC);

  for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
    K *key = keyidx_to_ptr(bucket->keyidx[i]);
    uint32_t pri = hash_nonzero(key);
    uint32_t sec = ht_hash_secondary(pri);
    struct ht_bucket *alt_bucket;
    int j;

    /* this entry is in its primary bucket? */
    if (pri == bucket->hv[i])
      alt_bucket = &buckets_[sec & bucket_mask_];
    else if (sec == bucket->hv[i])
      alt_bucket = &buckets_[pri & bucket_mask_];
    else
      assert(0);

    j = find_empty_slot(alt_bucket);
    if (j == -ENOSPC) j = make_space(alt_bucket, depth + 1);

    if (j >= 0) {
      /* Yay, we found one. Push recursively... */
      alt_bucket->hv[j] = bucket->hv[i];
      alt_bucket->keyidx[j] = bucket->keyidx[i];
      bucket->hv[i] = 0;
      return i;
    }
  }

  return -ENOSPC;
}

/* -ENOSPC if the bucket is full, 0 for success */
template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::add_to_bucket(struct ht_bucket *bucket, const K *key,
                                      const V *value) {
  for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
    if (bucket->hv[i] == 0) {
      void *entry;
      ht_keyidx_t k_idx = pop_free_keyidx();

      bucket->hv[i] = hash_nonzero(key);
      bucket->keyidx[i] = k_idx;

      entry = keyidx_to_ptr(k_idx);
      memcpy(entry, key, key_size_);
      memcpy((void *)((uintptr_t)entry + value_offset_), value, value_size_);

      cnt_++;
      return 0;
    }
  }

  return -ENOSPC;
}

/* the key must not already exist in the hash table */
template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::add_entry(uint32_t pri, uint32_t sec, const K *key,
                                  const V *value) {
  struct ht_bucket *pri_bucket;
  struct ht_bucket *sec_bucket;

again:
  pri_bucket = &buckets_[pri & bucket_mask_];
  if (add_to_bucket(pri_bucket, key, value) == 0) return 0;

  /* empty space in the secondary bucket? */
  sec_bucket = &buckets_[sec & bucket_mask_];
  if (add_to_bucket(sec_bucket, key, value) == 0) return 0;

  /* try kicking out someone in the primary bucket. */
  if (make_space(pri_bucket, 0) >= 0) goto again;

  /* try again from the secondary bucket */
  if (make_space(sec_bucket, 0) >= 0) goto again;

  return -ENOSPC;
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
V *HTable<K, V, C, H>::get_from_bucket(uint32_t pri, uint32_t hv,
                                       const K *key) const {
  uint32_t b_idx = hv & bucket_mask_;
  struct ht_bucket *bucket = &buckets_[b_idx];

  for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
    ht_keyidx_t k_idx;
    void *key_stored;

    if (pri != bucket->hv[i]) continue;

    k_idx = bucket->keyidx[i];
    key_stored = keyidx_to_ptr(k_idx);

    if (C(key, key_stored, key_size_) == 0)
      return (V *)((uintptr_t)key_stored + value_offset_);
  }

  return NULL;
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::del_from_bucket(uint32_t pri, uint32_t hv,
                                        const K *key) {
  uint32_t b_idx = hv & bucket_mask_;
  struct ht_bucket *bucket = &buckets_[b_idx];

  for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
    ht_keyidx_t k_idx;
    void *key_stored;

    if (pri != bucket->hv[i]) continue;

    k_idx = bucket->keyidx[i];
    key_stored = keyidx_to_ptr(k_idx);

    if (C(key, key_stored, key_size_) == 0) {
      bucket->hv[i] = 0;
      push_free_keyidx(k_idx);
      cnt_--;
      return 0;
    }
  }

  return -ENOENT;
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::InitEx(struct ht_params *params) {
  if (!params) return -EINVAL;

  if (params->key_size < 1) return -EINVAL;

  if (params->value_size < 0) return -EINVAL;

  if (params->key_align < 1 || params->key_align > 64) return -EINVAL;

  if (params->value_align < 0 || params->value_align > 64) return -EINVAL;

  if (params->value_size > 0 && params->value_align == 0) return -EINVAL;

  if (params->num_buckets < 1) return -EINVAL;

  if (params->num_buckets != align_ceil_pow2(params->num_buckets))
    return -EINVAL;

  if (params->num_entries < ENTRIES_PER_BUCKET) return -EINVAL;

  bucket_mask_ = params->num_buckets - 1;

  cnt_ = 0;
  num_entries_ = params->num_entries;
  free_keyidx_ = INVALID_KEYIDX;

  key_size_ = params->key_size;
  value_size_ = params->value_size;
  value_offset_ = align_ceil(key_size_, std::max(1ul, params->value_align));
  entry_size_ = align_ceil(value_offset_ + value_size_, params->key_align);

  buckets_ = (struct ht_bucket *)mem_alloc((bucket_mask_ + 1) *
                                           sizeof(struct ht_bucket));
  if (!buckets_) return -ENOMEM;

  entries_ = mem_alloc(num_entries_ * entry_size_);
  if (!entries_) {
    mem_free(buckets_);
    return -ENOMEM;
  }

  for (ht_keyidx_t i = num_entries_ - 1; i >= 0; i--) push_free_keyidx(i);

  return 0;
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::Init(size_t key_size, size_t value_size) {
  struct ht_params params = {};

  params.key_size = key_size;
  params.value_size = value_size;

  params.key_align = 1;

  if (value_size > 0 && value_size % 8 == 0)
    params.value_align = 8;
  else if (value_size > 0 && value_size % 4 == 0)
    params.value_align = 4;
  else if (value_size > 0 && value_size % 2 == 0)
    params.value_align = 2;
  else
    params.value_align = 1;

  params.num_buckets = INIT_NUM_BUCKETS;
  params.num_entries = INIT_NUM_ENTRIES;

  params.hash_func = NULL;
  params.keycmp_func = NULL;

  return InitEx(&params);
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
void HTable<K, V, C, H>::Close() {
  mem_free(buckets_);
  mem_free(entries_);
  memset(this, 0, sizeof(*this));
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
void HTable<K, V, C, H>::Clear() {
  uint32_t next = 0;
  K *key;

  while ((key = Iterate(&next))) Del(key);
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
V *HTable<K, V, C, H>::Get(const K *key) const {
  uint32_t pri = hash(key);

  return GetHash(pri, key);
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
V *HTable<K, V, C, H>::GetHash(uint32_t pri, const K *key) const {
  V *ret;

  pri = ht_make_nonzero(pri);

  /* check primary bucket */
  ret = get_from_bucket(pri, pri, key);
  if (ret) return ret;

  /* check secondary bucket */
  return get_from_bucket(pri, ht_hash_secondary(pri), key);
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::clone_table(HTable<K, V, C, H> *t_old,
                                    uint32_t num_buckets,
                                    ht_keyidx_t num_entries) {
  uint32_t next = 0;
  K *key;

  *this = *t_old;

  buckets_ =
      (struct ht_bucket *)mem_alloc(num_buckets * sizeof(struct ht_bucket));
  if (!buckets_) return -ENOMEM;

  entries_ = mem_alloc(num_entries * entry_size_);
  if (!entries_) {
    mem_free(buckets_);
    return -ENOMEM;
  }

  bucket_mask_ = num_buckets - 1;
  cnt_ = 0;
  num_entries_ = num_entries;
  free_keyidx_ = INVALID_KEYIDX;

  for (ht_keyidx_t i = num_entries_ - 1; i >= 0; i--) push_free_keyidx(i);

  while ((key = t_old->Iterate(&next))) {
    V *value = t_old->key_to_value(key);
    int ret = Set(key, value);

    if (ret) {
      Close();
      return ret;
    }
  }

  return 0;
}

/* may be called recursively */
template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::expand_buckets() {
  HTable<K, V, C, H> *t = new HTable<K, V, C, H>;
  uint32_t num_buckets = (bucket_mask_ + 1) * 2;

  assert(num_buckets == align_ceil_pow2(num_buckets));

  int ret = t->clone_table(this, num_buckets, num_entries_);
  if (ret == 0) {
    Close();
    *this = *t;
  }

  return ret;
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::Set(const K *key, const V *value) {
  uint32_t pri = hash(key);
  uint32_t sec = ht_hash_secondary(pri);

  int ret = 0;

  /* If the key already exists, its value is updated with the new one */
  void *old_value = GetHash(pri, key);
  if (old_value) {
    memcpy(old_value, value, value_size_);
    return 1;
  }

  pri = ht_make_nonzero(pri);
  sec = ht_hash_secondary(pri);

  while (add_entry(pri, sec, key, value) < 0) {
    /* expand the table as the last resort */
    ret = expand_buckets();
    if (ret < 0) break;
    /* retry on the newly expanded table */
  }

  return ret;
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::Del(const K *key) {
  uint32_t pri = hash_nonzero(key);
  uint32_t sec;

  if (del_from_bucket(pri, pri, key) == 0) return 0;

  sec = ht_hash_secondary(pri);
  if (del_from_bucket(pri, sec, key) == 0) return 0;

  return -ENOENT;
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
K *HTable<K, V, C, H>::Iterate(uint32_t *next) const {
  uint32_t idx = *next;

  uint32_t i;
  int j;

  do {
    i = idx / ENTRIES_PER_BUCKET;
    j = idx % ENTRIES_PER_BUCKET;

    if (i >= bucket_mask_ + 1) {
      *next = idx;
      return NULL;
    }

    idx++;
  } while (buckets_[i].hv[j] == 0);

  *next = idx;
  return keyidx_to_ptr(buckets_[i].keyidx[j]);
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::Count() const {
  return cnt_;
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
int HTable<K, V, C, H>::count_entries_in_pri_bucket() const {
  int ret = 0;

  for (uint32_t i = 0; i < bucket_mask_ + 1; i++) {
    for (int j = 0; j < ENTRIES_PER_BUCKET; j++) {
      uint32_t pri = buckets_[i].hv[j];
      if (pri && (pri & bucket_mask_) == i) ret++;
    }
  }

  return ret;
}

template <class K, class V, ht_keycmp_func_t C, ht_hash_func_t H>
void HTable<K, V, C, H>::Dump(int detail) const {
  int in_pri_bucket = count_entries_in_pri_bucket();

  printf("--------------------------------------------\n");

  if (detail) {
    for (uint32_t i = 0; i < bucket_mask_ + 1; i++) {
      printf("%4d:  ", i);

      for (int j = 0; j < ENTRIES_PER_BUCKET; j++) {
        uint32_t pri = buckets_[i].hv[j];
        uint32_t sec = ht_hash_secondary(pri);
        char type;

        if (!pri) {
          printf("  --------/-------- ----     ");
          continue;
        }

        if ((pri & bucket_mask_) == i) {
          if ((sec & bucket_mask_) != i)
            type = ' ';
          else
            type = '?';
        } else
          type = '!';

        printf("%c %08x/%08x %4d     ", type, pri, sec, buckets_[i].keyidx[j]);
      }

      printf("\n");
    }
  }

  printf("cnt = %d\n", cnt_);
  printf("entry array size = %d\n", num_entries_);
  printf("buckets = %d\n", bucket_mask_ + 1);
  printf("occupancy = %.1f%% (%.1f%% in primary buckets)\n",
         100.0 * cnt_ / ((bucket_mask_ + 1) * ENTRIES_PER_BUCKET),
         100.0 * in_pri_bucket / (cnt_ ?: 1));

  printf("key_size = %zu\n", key_size_);
  printf("value_size = %zu\n", value_size_);
  printf("value_offset = %zu\n", value_offset_);
  printf("entry_size = %zu\n", entry_size_);
  printf("\n");
}
#endif
