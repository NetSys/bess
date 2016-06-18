#include "../common.h"/* Streamlined hash table implementation, with emphasis on lookup performance. 
 * Key and value sizes are fixed. Lookup is thread-safe, but update is not. */

#ifndef _HTABLE_H_
#define _HTABLE_H_

#include <stdint.h>

#include <rte_config.h>
#include <rte_hash_crc.h>

#include "simd.h"

/* tunable macros */
#define INIT_NUM_BUCKETS	4
#define INIT_NUM_ENTRIES	16

/* 4^MAX_CUCKOO_PATH buckets will be considered to make a empty slot, 
 * before giving up and expand the table.
 * Higher number will yield better occupancy, but the worst case performance
 * of insertion will grow exponentially, so be careful. */
#define MAX_CUCKOO_PATH		3

/* non-tunable macros */
#define ENTRIES_PER_BUCKET	4	/* 4-way set associative */

#define DEFAULT_HASH_INITVAL	UINT32_MAX

typedef int32_t	ht_keyidx_t;

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

	uint32_t num_buckets;		/* must be a power of 2 */
	int num_entries;		/* >= 4 */

	ht_hash_func_t hash_func;
	ht_keycmp_func_t keycmp_func;
};

struct ht_bucket {
	uint32_t hv[ENTRIES_PER_BUCKET];
	ht_keyidx_t keyidx[ENTRIES_PER_BUCKET];
};

struct htable {
	/* bucket and entry arrays grow independently */
	struct ht_bucket *buckets;
	void *entries;		/* entry_size * num_entries bytes */ 

	ht_hash_func_t hash_func;
	ht_keycmp_func_t keycmp_func;

	/* # of buckets == mask + 1 */
	uint32_t bucket_mask;

	int cnt;			/* current number of entries */
	ht_keyidx_t num_entries;	/* current array size (# entries) */

	/* Linked list head for empty key slots (LIFO). NO_NEXT if empty */
	ht_keyidx_t free_keyidx;	

	/* in bytes */
	size_t key_size;
	size_t value_size;
	size_t value_offset;
	size_t entry_size;
};

/* -errno, or 0 for success */
int ht_init(struct htable *t, size_t key_size, size_t value_size);
int ht_init_ex(struct htable *t, struct ht_params *params);
void ht_close(struct htable *t);

/* returns NULL or the pointer to the data */
void *ht_get(const struct htable *t, const void *key);

/* identical to ht_get(), but you can supply a precomputed hash value "pri" */
void *ht_get_hash(const struct htable *t, uint32_t pri, const void *key);

/* -ENOMEM on error, 0 for succesful insertion, or 1 if updated */
int ht_set(struct htable *t, const void *key, const void *value);

/* -ENOENT on error, or 0 for success */
int ht_del(struct htable *t, const void *key);

/* Iterate over key pointers.
 * NULL if it reached the end of the table, or the pointer to the key.
 * User should set *next to 0 when starting iteration */
void *ht_iterate(const struct htable *t, uint32_t *next);

/* from the stored key pointer, return its value pointer */
static inline void *ht_key_to_value(const struct htable *t, const void *key)
{
	return (void *)(key + t->value_offset);
}

/* from DPDK */
static inline uint32_t ht_hash_secondary(uint32_t primary)
{
	uint32_t tag = primary >> 12;

	return primary ^ ((tag + 1) * 0x5bd1e995);
}

#define INVALID_KEYIDX	INT32_MAX

#if __AVX__
static inline ht_keyidx_t _get_keyidx_vec(const struct htable *t, uint32_t pri)
{
	struct ht_bucket *bucket = &t->buckets[pri & t->bucket_mask];

	__m128i v_pri = _mm_set1_epi32(pri);
	__m128i v_hv = _mm_loadu_si128((__m128i *)bucket->hv);
	__m128i v_cmp = _mm_cmpeq_epi32(v_hv, v_pri);
	int mask = _mm_movemask_epi8(v_cmp);
	int ffs = __builtin_ffs(mask);

	if (ffs > 0)
		return bucket->keyidx[ffs >> 2];

	uint32_t sec = ht_hash_secondary(pri);
	bucket = &t->buckets[sec & t->bucket_mask];

	v_hv = _mm_loadu_si128((__m128i *)bucket->hv);
	v_cmp = _mm_cmpeq_epi32(v_hv, v_pri);
	mask = _mm_movemask_epi8(v_cmp);
	ffs = __builtin_ffs(mask);

	if (ffs > 0)
		return bucket->keyidx[ffs >> 2];

	return INVALID_KEYIDX;
}
#else
  #define _get_keyidx_vec _get_keyidx
#endif

/* actually works faster for very small tables */
static inline ht_keyidx_t _get_keyidx(const struct htable *t, uint32_t pri)
{
	struct ht_bucket *bucket = &t->buckets[pri & t->bucket_mask];

	for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
		if (pri == bucket->hv[i])
			return bucket->keyidx[i];
	}

	uint32_t sec = ht_hash_secondary(pri);
	bucket = &t->buckets[sec & t->bucket_mask];
	for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
		if (pri == bucket->hv[i])
			return bucket->keyidx[i];
	}

	return INVALID_KEYIDX;
}

/* This macro provides an inlined (thus much faster) version for the lookup
 * operations. For example, suppose you have a custom hash table type "foo":
 * 
 * HT_DECLARE_INLINED_FUNCS(foo, uint64_t, uint8_t)
 * 
 * where uint64_t is the key type and uint8_t is the value type.
 * (you must use the same types when you call ht_init())
 * With this, you are required to define two functions (starting with "foo")
 * as follows:
 *
 * static inline int foo_keyeq(const uint64_t *key,
 * 		const uint64_t *key_stored, size_t key_len)
 * {
 *	// should return 0 if the two keys are identical, or a nonzero.
 * 	...
 * }
 *
 * static inline uint32_t foo_hash(const key_type *key, uint32_t key_len,
 * 		uint32_t init_val)
 * {
 * 	//
 * 	...
 * }
 *
 * NOTE: You can ignore key_len, since you already know the size of 
 *       key_type. It is there to make the functions compatible with memcpy()
 *       and rte_hash_function
 *
 * Once you define these functions, you can use ht_foo_hash(), which is a
 * faster version of ht_hash() with the same function prototype. */
#define HT_DECLARE_INLINED_FUNCS(name, key_type, value_type) 		\
									\
static inline int							\
name##_keyeq(const key_type *key, const key_type *key_stored,		\
		size_t key_len);					\
									\
static inline uint32_t							\
name##_hash(const key_type *key, uint32_t key_len, uint32_t init_val);	\
									\
static inline void *ht_##name##_get(const struct htable *t,		\
		const key_type *key)					\
{									\
	uint32_t pri = name##_hash(key, sizeof(key_type), 		\
			DEFAULT_HASH_INITVAL) | (1u << 31);		\
									\
	ht_keyidx_t k_idx = (t->cnt >= 2048) ?				\
			_get_keyidx_vec(t, pri) : _get_keyidx(t, pri);	\
	if (unlikely(k_idx == INVALID_KEYIDX)) 				\
		return NULL;						\
									\
	key_type *key_stored = t->entries + t->entry_size * k_idx;	\
									\
	/* Go to slow path if false positive. */			\
	if (likely(name##_keyeq(key, key_stored, sizeof(key_type))))	\
		return (void *)key_stored + t->value_offset;		\
	else								\
		return ht_get(t, key);					\
}

/* with non-zero 'detail', each item in the hash table will be shown */
void ht_dump(const struct htable *t, int detail);

#endif
