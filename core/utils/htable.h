/* Streamlined hash table implementation, with emphasis on lookup performance. 
 * Key and value sizes are fixed. Lookup is thread-safe, but update is not. */

#ifndef _HTABLE_H_
#define _HTABLE_H_

#include <stdint.h>

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

typedef int32_t	ht_keyidx_t;

struct ht_bucket {
	uint32_t hv[ENTRIES_PER_BUCKET];
	ht_keyidx_t keyidx[ENTRIES_PER_BUCKET];
};

struct htable {
	/* bucket and entry arrays grow independently */
	struct ht_bucket *buckets;
	void *entries;		/* entry_size * num_entries bytes */ 

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
void ht_close(struct htable *t);

/* returns NULL or the pointer to the data */
void *ht_get(const struct htable *t, const void *key);

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

#include <rte_hash_crc.h>

static inline uint32_t ht_hash(const void *key, size_t key_len)
{
	uint32_t init_val = UINT32_MAX;

	for (size_t i = 0; i < key_len / 8; i++) {
		init_val = rte_hash_crc_8byte(*(uint64_t *)key, init_val);
		key += 8;
	}

	if (key_len & 0x4) {
		init_val = rte_hash_crc_4byte(*(uint32_t *)key, init_val);
		key += 4;
	}

	if (key_len & 0x2) {
		init_val = rte_hash_crc_2byte(*(uint16_t *)key, init_val);
		key += 2;
	}

	if (key_len & 0x1)
		init_val = rte_hash_crc_1byte(*(uint8_t *)key, init_val);

	return init_val | (1ul << 31);	/* never returns 0 */
}

void ht_dump(const struct htable *t, int detail);
void ht_selftest();

#endif
