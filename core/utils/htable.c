#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <rte_config.h>
#include <rte_hash_crc.h>

#include "../common.h"
#include "../mem_alloc.h"

#include "htable.h"

#define DEFAULT_HASH_FUNC rte_hash_crc;

static inline uint32_t ht_hash(const struct htable *t, const void *key)
{
	return t->hash_func(key, t->key_size, UINT32_MAX);
}

static inline uint32_t ht_make_nonzero(uint32_t v)
{
	/* Set the MSB and unset the 2nd MSB (NOTE: must be idempotent).
	 * Then the result will never be a zero, and not a non-number when
	 * represented as float (so we are good to use _mm_*_ps() SIMD ops) */
	return (v | (1u << 31)) & (~(1u << 30));
}

static inline uint32_t ht_hash_nonzero(const struct htable *t, const void *key)
{
	return ht_make_nonzero(ht_hash(t, key));
}

static inline void *keyidx_to_ptr(const struct htable *t, ht_keyidx_t idx)
{
	return t->entries + t->entry_size * idx;
}

static inline ht_keyidx_t get_next(const struct htable *t, ht_keyidx_t curr)
{
	return *(ht_keyidx_t *)keyidx_to_ptr(t, curr);
}

static void push_free_keyidx(struct htable *t, ht_keyidx_t idx)
{
	assert(0 <= idx && idx < t->num_entries);

	*(ht_keyidx_t *)(t->entries + t->entry_size * idx) = t->free_keyidx;
	t->free_keyidx = idx;
}

/* entry array grows much more gently (50%) than bucket array (100%),
 * since space efficiency may be important for large keys and/or values. */
static int expand_entries(struct htable *t)
{
	ht_keyidx_t old_size = t->num_entries;
	ht_keyidx_t new_size = old_size + old_size / 2;

	void *new_entries;

	new_entries = mem_realloc(t->entries, new_size * t->entry_size);
	if (!new_entries)
		return -ENOMEM;

	t->num_entries = new_size;
	t->entries = new_entries;

	for (ht_keyidx_t i = new_size - 1; i >= old_size; i--)
		push_free_keyidx(t, i);

	return 0;
}

static ht_keyidx_t pop_free_keyidx(struct htable *t)
{
	ht_keyidx_t ret = t->free_keyidx;

	if (ret == INVALID_KEYIDX) {
		ret = expand_entries(t);
		if (ret)
			return ret;

		ret = t->free_keyidx;
	}

	t->free_keyidx = get_next(t, ret);

	return ret;
}

/* returns an empty slot ID, or -ENOSPC */
static int find_empty_slot(const struct ht_bucket *bucket)
{
	for (int i = 0; i < ENTRIES_PER_BUCKET; i++)
		if (bucket->hv[i] == 0)
			return i;

	return -ENOSPC;
}

/* Recursive function to try making an empty slot in the bucket.
 * Returns a slot ID in [0, ENTRIES_PER_BUCKET) for successful operation,
 * or -ENOSPC if failed */
static int make_space(struct htable *t, struct ht_bucket *bucket, int depth)
{
	if (depth >= MAX_CUCKOO_PATH)
		return -ENOSPC;

	/* Something is wrong if there's already an empty slot in this bucket */
	assert(find_empty_slot(bucket) == -ENOSPC);

	for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
		void *key = keyidx_to_ptr(t, bucket->keyidx[i]);
		uint32_t pri = ht_hash_nonzero(t, key);
		uint32_t sec = ht_hash_secondary(pri);
		struct ht_bucket *alt_bucket;
		int j;

		/* this entry is in its primary bucket? */
		if (pri == bucket->hv[i])
			alt_bucket = &t->buckets[sec & t->bucket_mask];
		else if (sec == bucket->hv[i])
			alt_bucket = &t->buckets[pri & t->bucket_mask];
		else
			assert(0);

		j = find_empty_slot(alt_bucket);
		if (j == -ENOSPC)
			j = make_space(t, alt_bucket, depth + 1);

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
static int add_to_bucket(struct htable *t, struct ht_bucket *bucket,
		const void *key, const void *value)
{
	for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
		if (bucket->hv[i] == 0) {
			void *entry;
			ht_keyidx_t k_idx = pop_free_keyidx(t);

			bucket->hv[i] = ht_hash_nonzero(t, key);
			bucket->keyidx[i] = k_idx;

			entry = keyidx_to_ptr(t, k_idx);
			memcpy(entry, key, t->key_size);
			memcpy(entry + t->value_offset, value, t->value_size);

			t->cnt++;
			return 0;
		}
	}

	return -ENOSPC;
}

/* the key must not already exist in the hash table */
static int add_entry(struct htable *t, uint32_t pri, uint32_t sec,
		const void *key, const void *value)
{
	struct ht_bucket *pri_bucket;
	struct ht_bucket *sec_bucket;

again:
	pri_bucket = &t->buckets[pri & t->bucket_mask];
	if (add_to_bucket(t, pri_bucket, key, value) == 0)
		return 0;

	/* empty space in the secondary bucket? */
	sec_bucket = &t->buckets[sec & t->bucket_mask];
	if (add_to_bucket(t, sec_bucket, key, value) == 0)
		return 0;

	/* try kicking out someone in the primary bucket. */
	if (make_space(t, pri_bucket, 0) >= 0)
		goto again;

	/* try again from the secondary bucket */
	if (make_space(t, sec_bucket, 0) >= 0)
		goto again;

	return -ENOSPC;
}

static int clone_table(struct htable *t_new, struct htable *t_old,
		uint32_t num_buckets, ht_keyidx_t num_entries)
{
	uint32_t next = 0;
	void *key;

	*t_new = *t_old;

	t_new->buckets = mem_alloc(num_buckets * sizeof(struct ht_bucket));
	if (!t_new->buckets)
		return -ENOMEM;

	t_new->entries = mem_alloc(num_entries * t_new->entry_size);
	if (!t_new->entries) {
		mem_free(t_new->buckets);
		return -ENOMEM;
	}

	t_new->bucket_mask = num_buckets - 1;
	t_new->cnt = 0;
	t_new->num_entries = num_entries;
	t_new->free_keyidx = INVALID_KEYIDX;

	for (ht_keyidx_t i = t_new->num_entries - 1; i >= 0; i--)
		push_free_keyidx(t_new, i);

	while ((key = ht_iterate(t_old, &next))) {
		void *value = ht_key_to_value(t_old, key);
		int ret = ht_set(t_new, key, value);

		if (ret) {
			ht_close(t_new);
			return ret;
		}
	}

	return 0;
}

/* may be called recursively */
static int expand_buckets(struct htable *t_old)
{
	struct htable t;
	uint32_t num_buckets = (t_old->bucket_mask + 1) * 2;

	assert(num_buckets == align_ceil_pow2(num_buckets));

	int ret = clone_table(&t, t_old, num_buckets, t_old->num_entries);
	if (ret == 0) {
		ht_close(t_old);
		*t_old = t;
	}

	return ret;
}

static void *get_from_bucket(const struct htable *t,
		uint32_t pri, uint32_t hv, const void *key)
{
	uint32_t b_idx = hv & t->bucket_mask;
	struct ht_bucket *bucket = &t->buckets[b_idx];

	for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
		ht_keyidx_t k_idx;
		void *key_stored;

		if (pri != bucket->hv[i])
			continue;

		k_idx = bucket->keyidx[i];
		key_stored = keyidx_to_ptr(t, k_idx);

		if (t->keycmp_func(key, key_stored, t->key_size) == 0)
			return key_stored + t->value_offset;
	}

	return NULL;
}

static int del_from_bucket(struct htable *t,
		uint32_t pri, uint32_t hv, const void *key)
{
	uint32_t b_idx = hv & t->bucket_mask;
	struct ht_bucket *bucket = &t->buckets[b_idx];

	for (int i = 0; i < ENTRIES_PER_BUCKET; i++) {
		ht_keyidx_t k_idx;
		void *key_stored;

		if (pri != bucket->hv[i])
			continue;

		k_idx = bucket->keyidx[i];
		key_stored = keyidx_to_ptr(t, k_idx);

		if (t->keycmp_func(key, key_stored, t->key_size) == 0) {
			bucket->hv[i] = 0;
			push_free_keyidx(t, k_idx);
			t->cnt--;
			return 0;
		}
	}

	return -ENOENT;
}

int ht_init_ex(struct htable *t, struct ht_params *params)
{
	if (!t || !params)
		return -EINVAL;

	if (params->key_size < 1)
		return -EINVAL;

	if (params->value_size < 0)
		return -EINVAL;

	if (params->key_align < 1 || params->key_align > 64)
		return -EINVAL;

	if (params->value_align < 0 || params->value_align > 64)
		return -EINVAL;

	if (params->value_size > 0 && params->value_align == 0)
		return -EINVAL;

	if (params->num_buckets < 1)
		return -EINVAL;

	if (params->num_buckets != align_ceil_pow2(params->num_buckets))
		return -EINVAL;

	if (params->num_entries < ENTRIES_PER_BUCKET)
		return -EINVAL;

	memset(t, 0, sizeof(*t));

	t->hash_func = params->hash_func ? : DEFAULT_HASH_FUNC;
	t->keycmp_func = params->keycmp_func ? : memcmp;

	t->bucket_mask = params->num_buckets - 1;

	t->cnt = 0;
	t->num_entries = params->num_entries;
	t->free_keyidx = INVALID_KEYIDX;

	t->key_size = params->key_size;
	t->value_size = params->value_size;
	t->value_offset = align_ceil(t->key_size, MAX(1, params->value_align));
	t->entry_size = align_ceil(t->value_offset + t->value_size,
			params->key_align);

	t->buckets = mem_alloc((t->bucket_mask + 1) * sizeof(struct ht_bucket));
	if (!t->buckets)
		return -ENOMEM;

	t->entries = mem_alloc(t->num_entries * t->entry_size);
	if (!t->entries) {
		mem_free(t->buckets);
		return -ENOMEM;
	}

	for (ht_keyidx_t i = t->num_entries - 1; i >= 0; i--)
		push_free_keyidx(t, i);

	return 0;
}

int ht_init(struct htable *t, size_t key_size, size_t value_size)
{
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

	return ht_init_ex(t, &params);
}

void ht_close(struct htable *t)
{
	mem_free(t->buckets);
	mem_free(t->entries);
	memset(t, 0, sizeof(*t));
}

void ht_clear(struct htable *t)
{
	uint32_t next = 0;
	void *key;

	while ((key = ht_iterate(t, &next)))
		ht_del(t, key);
}

void *ht_get(const struct htable *t, const void *key)
{
	uint32_t pri = ht_hash(t, key);

	return ht_get_hash(t, pri, key);
}

void *ht_get_hash(const struct htable *t, uint32_t pri, const void *key)
{
	void *ret;

	pri = ht_make_nonzero(pri);

	/* check primary bucket */
	ret = get_from_bucket(t, pri, pri, key);
	if (ret)
		return ret;

	/* check secondary bucket */
	return get_from_bucket(t, pri, ht_hash_secondary(pri), key);
}

int ht_set(struct htable *t, const void *key, const void *value)
{
	uint32_t pri = ht_hash(t, key);
	uint32_t sec = ht_hash_secondary(pri);

	int ret = 0;

	/* If the key already exists, its value is updated with the new one */
	void *old_value = ht_get_hash(t, pri, key);
	if (old_value) {
		memcpy(old_value, value, t->value_size);
		return 1;
	}

	pri = ht_make_nonzero(pri);
	sec = ht_hash_secondary(pri);

	while (add_entry(t, pri, sec, key, value) < 0) {
		/* expand the table as the last resort */
		ret = expand_buckets(t);
		if (ret < 0)
			break;
		/* retry on the newly expanded table */
	}

	return ret;
}

int ht_del(struct htable *t, const void *key)
{
	uint32_t pri = ht_hash_nonzero(t, key);
	uint32_t sec;

	if (del_from_bucket(t, pri, pri, key) == 0)
		return 0;

	sec = ht_hash_secondary(pri);
	if (del_from_bucket(t, pri, sec, key) == 0)
		return 0;

	return -ENOENT;
}

void *ht_iterate(const struct htable *t, uint32_t *next)
{
	uint32_t idx = *next;

	uint32_t i;
	int j;

	do {
		i = idx / ENTRIES_PER_BUCKET;
		j = idx % ENTRIES_PER_BUCKET;

		if (i >= t->bucket_mask + 1) {
			*next = idx;
			return NULL;
		}

		idx++;
	} while (t->buckets[i].hv[j] == 0);

	*next = idx;
	return keyidx_to_ptr(t, t->buckets[i].keyidx[j]);
}

static int count_entries_in_pri_bucket(const struct htable *t)
{
	int ret = 0;

	for (uint32_t i = 0; i < t->bucket_mask + 1; i++) {
		for (int j = 0; j < ENTRIES_PER_BUCKET; j++) {
			uint32_t pri = t->buckets[i].hv[j];
			if (pri && (pri & t->bucket_mask) == i)
				ret++;
		}
	}

	return ret;
}

void ht_dump(const struct htable *t, int detail)
{
	int in_pri_bucket = count_entries_in_pri_bucket(t);

	printf("--------------------------------------------\n");

	if (detail) {
		for (uint32_t i = 0; i < t->bucket_mask + 1; i++) {
			printf("%4d:  ", i);

			for (int j = 0; j < ENTRIES_PER_BUCKET; j++) {
				uint32_t pri = t->buckets[i].hv[j];
				uint32_t sec = ht_hash_secondary(pri);
				char type;

				if (!pri) {
					printf("  --------/-------- ----     ");
					continue;
				}

				if ((pri & t->bucket_mask) == i) {
					if ((sec & t->bucket_mask) != i)
						type = ' ';
					else
						type = '?';
				} else
					type = '!';

				printf("%c %08x/%08x %4d     ",
					type, pri, sec,
					t->buckets[i].keyidx[j]);
			}

			printf("\n");
		}
	}

	printf("cnt = %d\n", t->cnt);
	printf("entry array size = %d\n", t->num_entries);
	printf("buckets = %d\n", t->bucket_mask + 1);
	printf("occupancy = %.1f%% (%.1f%% in primary buckets)\n",
			100.0 * t->cnt /
				((t->bucket_mask + 1) * ENTRIES_PER_BUCKET),
			100.0 * in_pri_bucket / (t->cnt ? : 1));

	printf("key_size = %zu\n", t->key_size);
	printf("value_size = %zu\n", t->value_size);
	printf("value_offset = %zu\n", t->value_offset);
	printf("entry_size = %zu\n", t->entry_size);
	printf("\n");
}
