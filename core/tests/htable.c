#include <assert.h>
#include <stdio.h>
#include <math.h>

#include <rte_config.h>
#include <rte_hash.h>
#include <rte_hash_crc.h>

#include "../utils/htable.h"
#include "../utils/random.h"
#include "../time.h"
#include "../common.h"
#include "../mem_alloc.h"

#include "../test.h"

#define bulk_size	16

typedef uint16_t value_t;

HT_DECLARE_INLINED_FUNCS(inlined, uint32_t)

static inline int 
inlined_keycmp(const uint32_t *key, const uint32_t *key_stored, size_t key_size)
{
	return *key != *key_stored;
}

static inline uint32_t
inlined_hash(const uint32_t *key, uint32_t key_size, uint32_t init_val)
{
#if __SSE4_2__
	return crc32c_sse42_u32(*key, init_val);
#else
	return rte_hash_crc_4byte(*(uint32_t *)key, init_val);
#endif
}

static inline value_t derive_val(uint32_t key)
{
	return (value_t)(key + 3);
}

static inline uint32_t rand_fast_nonzero(uint64_t *seed)
{
	uint32_t ret;
	
	/* work around with the bug with zero key in DPDK hash table */
	do {
		ret = rand_fast(seed);
	} while (unlikely(ret == 0));

	return ret;
}

static void *bess_init(int entries)
{
	struct htable *t;
	uint64_t seed = 0;

	t = mem_alloc(sizeof(*t));
	if (!t)
		return NULL;

	ht_init(t, sizeof(uint32_t), sizeof(value_t));

	for (int i = 0; i < entries; i++) {
		uint32_t key = rand_fast(&seed);
		value_t val = derive_val(key);

		int ret = ht_set(t, &key, &val);
		if (ret == -ENOMEM) {
			ht_close(t);
			return NULL;
		} else
			assert(ret == 0 || ret == 1);
	}

	return t;
}

static void bess_get(void *arg, int iteration, int entries)
{
	struct htable *t = arg;

	for (int k = 0; k < iteration; k++) {
		uint64_t seed = 0;
		for (int i = 0; i < entries; i++) {
			uint32_t key = rand_fast(&seed);
			value_t *val;

			val = ht_get(t, &key);
			assert(val && *val == derive_val(key));
		}
	}
}

static void bess_inlined_get(void *arg, int iteration, int entries)
{
	struct htable *t = arg;

	for (int k = 0; k < iteration; k++) {
		uint64_t seed = 0;
		for (int i = 0; i < entries; i++) {
			uint32_t key = rand_fast(&seed);
			value_t *val;

			val = ht_inlined_get(t, &key);
			assert(val && *val == derive_val(key));
		}
	}
}

static void bess_inlined_get_bulk(void *arg, int iteration, int entries)
{
	struct htable *t = arg;

	for (int k = 0; k < iteration; k++) {
		uint64_t seed = 0;
		for (int i = 0; i < entries; i += bulk_size) {
			uint32_t keys[bulk_size];
			uint32_t *key_ptrs[bulk_size];
			value_t *data_ptrs[bulk_size];

			int size = MIN(bulk_size, entries - i);

			for (int j = 0; j < size; j++) {
				keys[j] = rand_fast(&seed);
				key_ptrs[j] = &keys[j];
			}

			ht_inlined_get_bulk(t, size, (const void **)key_ptrs, 
					(void **)data_ptrs);

			for (int j = 0; j < size; j++) {
				value_t *p = data_ptrs[j];
				assert(p && *p == derive_val(keys[j]));
			}
		}
	}
}

static void bess_close(void *arg)
{
	struct htable *t = arg;

	ht_close(t);
}

struct dpdk_ht {
	struct rte_hash *t;
	value_t *value_arr;
};

static void *dpdk_discrete_init(int entries)
{
	struct dpdk_ht *ht;

	struct rte_hash *t;
	value_t *value_arr;

	struct rte_hash_parameters params;
	uint64_t seed = 0;

	params = (struct rte_hash_parameters) {
		.name = "rte_hash_test",
		.entries = align_ceil_pow2(MAX(8, entries * 2)),
		.key_len = sizeof(uint32_t),
		.hash_func = rte_hash_crc,
		.hash_func_init_val = UINT32_MAX,
		.socket_id = 0,		/* XXX */
	};

	t = rte_hash_create(&params);
	if (!t)
		return NULL;

	value_arr = mem_alloc(sizeof(value_t) * entries);
	if (!value_arr) {
		rte_hash_free(t);
		return NULL;
	}

	ht = mem_alloc(sizeof(struct dpdk_ht));
	if (!ht) {
		mem_free(value_arr);
		rte_hash_free(t);
		return NULL;
	}

	for (int i = 0; i < entries; i++) {
		uint32_t key = rand_fast_nonzero(&seed);
		value_t val = derive_val(key);

		int ret = rte_hash_add_key(t, &key);
		assert(ret >= 0);
		value_arr[ret] = val;
	}

	ht->t = t;
	ht->value_arr = value_arr;

	return ht;
}

static void *dpdk_embedded_init(int entries)
{
	struct rte_hash *t;

	struct rte_hash_parameters params;
	uint64_t seed = 0;

	params = (struct rte_hash_parameters) {
		.name = "rte_hash_test",
		.entries = align_ceil_pow2(MAX(8, entries * 2)),
		.key_len = sizeof(uint32_t),
		.hash_func = rte_hash_crc,
		.hash_func_init_val = UINT32_MAX,
		.socket_id = 0,		/* XXX */
	};

	t = rte_hash_create(&params);
	if (!t)
		return NULL;

	for (int i = 0; i < entries; i++) {
		uint32_t key = rand_fast_nonzero(&seed);
		uintptr_t val = derive_val(key);

		int ret = rte_hash_add_key_data(t, &key, (void *)val);
		assert(ret == 0);
	}

	return t;
}

static void dpdk_(void *arg, int iteration, int entries)
{
	struct dpdk_ht *ht = arg;
	struct rte_hash *t = ht->t;
	value_t *value_arr = ht->value_arr;

	for (int k = 0; k < iteration; k++) {
		uint64_t seed = 0;
		for (int i = 0; i < entries; i++) {
			uint32_t key = rand_fast_nonzero(&seed);

			int ret = rte_hash_lookup(t, &key);
			assert(ret >= 0 && value_arr[ret] == derive_val(key));
		}
	}
}

static void dpdk_hash(void *arg, int iteration, int entries)
{
	struct dpdk_ht *ht = arg;
	struct rte_hash *t = ht->t;
	value_t *value_arr = ht->value_arr;

	for (int k = 0; k < iteration; k++) {
		uint64_t seed = 0;
		for (int i = 0; i < entries; i++) {
			uint32_t key = rand_fast_nonzero(&seed);

			int ret = rte_hash_lookup_with_hash(t, &key,
					crc32c_sse42_u32(key, UINT32_MAX));
			assert(ret >= 0 && value_arr[ret] == derive_val(key));
		}
	}
}

static void dpdk_bulk(void *arg, int iteration, int entries)
{
	struct dpdk_ht *ht = arg;
	struct rte_hash *t = ht->t;
	value_t *value_arr = ht->value_arr;

	for (int k = 0; k < iteration; k++) {
		uint64_t seed = 0;
		for (int i = 0; i < entries; i += bulk_size) {
			uint32_t keys[bulk_size];
			uint32_t *key_ptrs[bulk_size];
			int32_t positions[bulk_size];

			int size = MIN(bulk_size, entries - i);

			for (int j = 0; j < size; j++) {
				keys[j] = rand_fast_nonzero(&seed);
				key_ptrs[j] = &keys[j];
			}

			rte_hash_lookup_bulk(t, (const void **)key_ptrs, 
					size, positions);

			for (int j = 0; j < size; j++) {
				int idx = positions[j];
				assert(idx >= 0 && value_arr[idx] == derive_val(keys[j]));
			}
		}
	}
}


static void dpdk_data(void *arg, int iteration, int entries)
{
	struct rte_hash *t = arg;

	for (int k = 0; k < iteration; k++) {
		uint64_t seed = 0;
		for (int i = 0; i < entries; i++) {
			uint32_t key = rand_fast_nonzero(&seed);
			uintptr_t val;

			rte_hash_lookup_data(t, &key, (void **)&val);
			assert((value_t)val == derive_val(key));
		}
	}
}

static void dpdk_data_hash(void *arg, int iteration, int entries)
{
	struct rte_hash *t = arg;

	for (int k = 0; k < iteration; k++) {
		uint64_t seed = 0;
		for (int i = 0; i < entries; i++) {
			uint32_t key = rand_fast_nonzero(&seed);
			uintptr_t val;

			rte_hash_lookup_with_hash_data(t, &key, 
					crc32c_sse42_u32(key, UINT32_MAX), 
					(void **)&val);
			assert((value_t)val == derive_val(key));
		}
	}
}

static void dpdk_data_bulk(void *arg, int iteration, int entries)
{
	struct rte_hash *t = arg;

	for (int k = 0; k < iteration; k++) {
		uint64_t seed = 0;
		for (int i = 0; i < entries; i += bulk_size) {
			uint32_t keys[bulk_size];
			uint32_t *key_ptrs[bulk_size];
			uintptr_t data[bulk_size];
			uint64_t hit_mask;

			int size = MIN(bulk_size, entries - i);

			for (int j = 0; j < size; j++) {
				keys[j] = rand_fast_nonzero(&seed);
				key_ptrs[j] = &keys[j];
			}

			rte_hash_lookup_bulk_data(t, (const void **)key_ptrs, 
					size, &hit_mask, (void **)&data);

			assert(hit_mask == (1ul << size) - 1);
			for (int j = 0; j < size; j++)
				assert((value_t)data[j] == derive_val(keys[j]));
		}
	}
}

static void dpdk_embedded_close(void *arg)
{
	struct rte_hash *t = arg;

	rte_hash_free(t);
}

static void dpdk_discrete_close(void *arg)
{
	struct dpdk_ht *ht = arg;
	struct rte_hash *t = ht->t;
	value_t *value_arr = ht->value_arr;

	rte_hash_free(t);
	mem_free(value_arr);
	mem_free(ht);
}

struct player {
	const char *name;
	void *(*init)(int entries);
	void (*lookup)(void *arg, int iteration, int entries);
	void (*close)(void *arg);
};

/* 4-byte key, 2-byte value */
static void perftest()
{
#if 1
	const int test_entries[] = {
		1, 4, 16, 64, 256, 1024, 4096, 
		16384, 65536, 262144, 1048576, 4194304
	};
#else
	const int test_entries[] = {
		16, 64, 256, 1024, 4096, 
		16384, 65536, 262144, 1048576, 4194304
	};

	const int test_entries[] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9,
		10, 20, 30, 40, 50, 60, 70, 80, 90, 
	};

	const int test_entries[] = {
		10, 100, 1000, 10000, 100000,
		1000000, 10000000
	};

	const int test_entries[] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9,
		10, 20, 30, 40, 50, 60, 70, 80, 90, 
	};

	const int test_entries[] = {
		100, 200, 300, 400, 500, 600, 700, 800, 900,
		1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000,
	};

	const int test_entries[] = {
		10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000,
		100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000, 900000,
	};

	const int test_entries[] = {
		1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000,
		10000000, 20000000, 30000000, 40000000, 50000000, 60000000, 70000000, 80000000, 90000000,
	};
#endif

	const struct player players[] = {
		{"ht_get", bess_init, bess_get, bess_close},
		{"ht_inlined_get", bess_init, bess_inlined_get, bess_close},
		{"ht_inlined_get_bulk(x16)", bess_init, 
			bess_inlined_get_bulk, bess_close},
		{"rte_hash_lookup", dpdk_discrete_init, 
			dpdk_, dpdk_discrete_close},
		{"rte_hash_lookup_with_hash", dpdk_discrete_init, 
			dpdk_hash, dpdk_discrete_close},
		{"rte_hash_lookup_bulk(x16)", dpdk_discrete_init, 
			dpdk_bulk, dpdk_discrete_close},
		{"rte_hash_lookup_data", dpdk_embedded_init, 
			dpdk_data, dpdk_embedded_close},
		{"rte_hash_lookup_with_hash_data", dpdk_embedded_init, 
			dpdk_data_hash, dpdk_embedded_close},
		{"rte_hash_lookup_bulk_data(x16)", dpdk_embedded_init,
			dpdk_data_bulk, dpdk_embedded_close},
	};

	printf("%-32s", "Functions,Mops");
	for (int i = 0; i < ARR_SIZE(test_entries); i++)
		printf("%9d", test_entries[i]);
	printf("\n");

	for (int i = 0; i < ARR_SIZE(players); i++) {
		const struct player *p = &players[i];
		printf("%-32s", p->name);
		for (int j = 0; j < ARR_SIZE(test_entries); j++) {
			int entries = test_entries[j];
			int iteration = MAX(1, (int)(1e6 / entries));
			void *arg;
			double elapsed = NAN;

			arg = p->init(entries);
			if (!arg)
				break;

			fflush(stdout);

			double start_time = get_cpu_time();
			int total_iteration = 0;

			do {
				p->lookup(arg, iteration, entries);
				elapsed = get_cpu_time() - start_time;
				total_iteration += iteration;
			} while (elapsed < 1);

			printf("%9.1f", (total_iteration * entries) / 
					(elapsed * 1e6));

			p->close(arg);
		}
		printf("\n");
	}
}

static void functest()
{
	struct htable t;
	uint64_t seed;

	const int iteration = 1000000;
	int num_updates = 0;

	ht_init(&t, sizeof(uint32_t), sizeof(uint16_t));

	seed = 0;
	for (int i = 0; i < iteration; i++) {
		uint32_t key = rand_fast(&seed);
		uint16_t val = derive_val(key);
		int ret;

		ret = ht_set(&t, &key, &val);
		if (ret == 1)
			num_updates++;
		else
			assert(ret == 0);
	}

	seed = 0;
	for (int i = 0; i < iteration; i++) {
		uint32_t key = rand_fast(&seed);
		uint16_t *val;

		val = ht_get(&t, &key);
		assert(val != NULL);
		assert(*val == derive_val(key));
	}

	seed = 0;
	for (int i = 0; i < iteration; i++) {
		uint32_t key = rand_fast(&seed);
		int ret;

		ret = ht_del(&t, &key);
		if (ret == -ENOENT)
			num_updates--;
		else
			assert(ret == 0);
	}

	assert(num_updates == 0);
	assert(t.cnt == 0);

	ht_close(&t);
}

ADD_TEST(perftest, "hash table performance comparison")
ADD_TEST(functest, "hash table correctness test")
