#include "../module.h"

#include <rte_hash_crc.h>
#include <rte_prefetch.h>

#define MAX_TABLE_SIZE (1048576 * 32)
#define MAX_BUCKET_SIZE (4)

#define RESERVED_OCCUPIED_BIT (0x1ul)

#define MAX_INSERTION_SEARCH_DEPTH (2)

#define L2_BROADCAST_GATE (UINT16_MAX - 1)
#define L2_INVALID_GATE (INVALID_GATE)

#define MAX_LOOKUP_BATCH (4)

#define PREFETCH (1)

struct l2_entry
{
	uint64_t addr;
	uint32_t gate;
	uint32_t reserved;
};

struct l2_table
{
	struct l2_entry *table;
	uint64_t size;
	uint64_t size_power;
	uint64_t bucket;
};

typedef uint64_t mac_addr_t;
typedef uint16_t gate_t;

static int is_power_of_2(uint64_t n)
{
	return (n != 0 && ((n & (n - 1)) == 0));
}

/*
 * l2_init: 
 *  Initilizes the l2_table. 
 *  It creates the slots of MAX_TABLE_SIZE multiplied by MAX_BUCKET_SIZE.
 * 
 * @l2tbl: pointer to 
 * @size: number of hash value entries. must be power of 2, greater than 0, and
 *        less than equal to MAX_TABLE_SIZE (2^30)
 * @bucket: number of slots per hash value. must be power of 2, greater than 0,
 *        and less than equal to MAX_BUCKET_SIZE (4)
 */
static int l2_init(struct l2_table *l2tbl, int size, int bucket)
{
	if (size <= 0 || size > MAX_TABLE_SIZE ||
	    !is_power_of_2(size))
		return -EINVAL;
	
	if (bucket <= 0 || bucket > MAX_BUCKET_SIZE ||
	    !is_power_of_2(bucket))
		return -EINVAL;

	if (l2tbl == NULL)
		return -EINVAL;

#if 0
	l2tbl->table =
		rte_zmalloc("l2tbl",
			    sizeof(struct l2_entry) * size * bucket, 0);
#else
	l2tbl->table =
		malloc(sizeof(struct l2_entry) * size * bucket);
#endif
	
	if (l2tbl->table == NULL)
		return -ENOMEM;

	l2tbl->size = size;
	l2tbl->bucket = bucket;

	printf("size: %d\n", size);
	
	/* calculates the log_2 (size) */	
	l2tbl->size_power = 0;
	while (size > 1) {
		size = size >> 1;
		l2tbl->size_power += 1;
	}


	return 0;
}

static int l2_deinit(struct l2_table *l2tbl)
{
	if (l2tbl == NULL)
		return -EINVAL;

	if (l2tbl->table == NULL)
		return -EINVAL;

	if (l2tbl->size == 0)
		return -EINVAL;

	if (l2tbl->bucket == 0)
		return -EINVAL;

#if 0
	rte_free(l2tbl->table);
#else
	free(l2tbl->table);
#endif

	memset(l2tbl, 0, sizeof(struct l2_table));

	return 0;
}

static uint32_t l2_ib_to_offset(struct l2_table *l2tbl, int index, int bucket)
{
	return index * l2tbl->bucket + bucket;
}

static uint32_t l2_hash(mac_addr_t addr)
{
	return rte_hash_crc_8byte(addr, 0);
}

static uint32_t l2_hash_to_index(uint32_t hash, uint32_t size)
{
	return hash & (size - 1);
}

static uint32_t l2_alt_index(uint32_t hash, uint32_t size_power, uint32_t index)
{
	uint64_t tag =  (hash >> size_power) + 1;
	tag = tag * 0x5bd1e995;
	return (index ^ tag) & ((0x1lu << (size_power - 1)) - 1);
}

static int l2_find(struct l2_table *l2tbl,
		   uint64_t addr, gate_t *gate, uint32_t *offset_out)
{
	int i;
	uint32_t hash, idx1, offset;
	struct l2_entry *tbl = l2tbl->table;

	hash = l2_hash(addr);
	idx1 = l2_hash_to_index(hash, l2tbl->size);


	offset = l2_ib_to_offset(l2tbl, idx1, 0);
	/* search buckets for first index */
	for (i = 0; i < l2tbl->bucket; i++) {

		if ((tbl[offset].reserved & RESERVED_OCCUPIED_BIT) &&
		    addr == tbl[offset].addr) {
			*gate = tbl[offset].gate;
			//*offset_out = offset;
			return 0;
		}

		offset++;
	}

	idx1 = l2_alt_index(hash, l2tbl->size_power, idx1);	
	offset = l2_ib_to_offset(l2tbl, idx1, 0);	
	/* search buckets for alternate index */
	for (i = 0; i < l2tbl->bucket; i++) {
		if ((tbl[offset].reserved & RESERVED_OCCUPIED_BIT) &&
		    addr == tbl[offset].addr) {
			*gate = tbl[offset].gate;
			//*offset_out = offset;
			return 0;
		}
		
		offset++;
	}

	return -ENOENT;
}

static int l2_find_slot(struct l2_table *l2tbl, mac_addr_t addr,
			uint32_t *idx, uint32_t *bucket)
{
	int i, j;
	uint32_t hash;
	uint32_t idx1, idx_v1, idx_v2;
	uint32_t offset1, offset2;
	struct l2_entry *tbl = l2tbl->table;

	hash = l2_hash(addr);
	idx1 = l2_hash_to_index(hash, l2tbl->size);
	
	/* if there is available slot */
	for (i = 0; i < l2tbl->bucket; i++) {
		offset1 = l2_ib_to_offset(l2tbl, idx1, i);
		if (!(tbl[offset1].reserved & RESERVED_OCCUPIED_BIT)) {
			*idx = idx1;
			*bucket = i;
			return 0;
		}
	}

	offset1 = l2_ib_to_offset(l2tbl, idx1, 0);

	/* try moving */
	for (i = 0; i < l2tbl->bucket; i++) {
		offset1 = l2_ib_to_offset(l2tbl, idx1, i);
		hash = l2_hash(tbl[offset1].addr);
		idx_v1 = l2_hash_to_index(hash, l2tbl->size);
		idx_v2 = l2_alt_index(hash, l2tbl->size_power, idx_v1);

		/* if the alternate bucket is same as original skip it */
		if (idx_v1 == idx_v2 || idx1 == idx_v2)
			break;
		
		for (j = 0; j < l2tbl->bucket; j++) {
			offset2 = l2_ib_to_offset(l2tbl, idx_v2, j);
			if (!(tbl[offset2].reserved & RESERVED_OCCUPIED_BIT)) {
				/* move offset1 to offset2 */
				tbl[offset2] = tbl[offset1];
				/* clear offset1 */
				tbl[offset1].reserved = 0;
			
				*idx = idx1;
				*bucket = 0;
				return 0;
			}
		}
	}

	/* TODO:if alternate index is also full then start move */
	return -ENOMEM;
}

static int l2_add_entry(struct l2_table *l2tbl, mac_addr_t addr, gate_t gate)
{
	uint32_t offset;
	uint32_t index;
	uint32_t bucket;
	gate_t gate_tmp;

	/* if addr already exist then fail */
	if (l2_find(l2tbl, addr, &gate_tmp, &offset) == 0) {
		return -EEXIST;
	}

	/* find slots to put entry */
	if (l2_find_slot(l2tbl, addr, &index, &bucket) != 0) {
		return -ENOMEM;
	}

	/* insert entry into empty slot */
	offset = l2_ib_to_offset(l2tbl, index, bucket);

	l2tbl->table[offset].addr = addr;
	l2tbl->table[offset].gate = gate;
	l2tbl->table[offset].reserved |= RESERVED_OCCUPIED_BIT;
	return 0;
}

static int l2_del_entry(struct l2_table *l2tbl, uint64_t addr)
{
	uint32_t offset = 0xFFFFFFFF;
	gate_t gate;

	if (l2_find(l2tbl, addr, &gate, &offset)) {
		return -ENOENT;
	}

	l2tbl->table[offset].addr = 0;
	l2tbl->table[offset].gate = 0;
	l2tbl->table[offset].reserved = 0;

	return 0;
}



__attribute__((optimize("unroll-loops")))
static void l2_find_batch(struct l2_table *l2tbl, uint64_t *addr,
			  gate_t *result, int count)
{
	int i, j;
	uint32_t hash[MAX_LOOKUP_BATCH], idx1[MAX_LOOKUP_BATCH];
	uint32_t idx2[MAX_LOOKUP_BATCH], offset;
	struct l2_entry *tbl = l2tbl->table;	

	assert(count == MAX_LOOKUP_BATCH);
	
	for (i = 0; i < MAX_LOOKUP_BATCH; i++) {
		hash[i] = l2_hash(addr[i]);
		idx1[i] = l2_hash_to_index(hash[i], l2tbl->size);
		idx2[i] = l2_alt_index(hash[i], l2tbl->size_power, idx1[i]);
		result[i] = INVALID_GATE;
#if PREFETCH
		offset = l2_ib_to_offset(l2tbl, idx1[i], 0);
		rte_prefetch0(&tbl[offset]);
		offset = l2_ib_to_offset(l2tbl, idx2[i], 0);		
		rte_prefetch0(&tbl[offset]);
#endif
	}

	for (i = 0; i < MAX_LOOKUP_BATCH; i++) {
		offset = l2_ib_to_offset(l2tbl, idx1[i], 0);
		
		for (j = 0; j < l2tbl->bucket; j++) {
			if ((tbl[offset].reserved & RESERVED_OCCUPIED_BIT) &&
			    addr[i] == tbl[offset].addr) {
				result[i] = tbl[offset].gate;
				break;
			}

			offset++;
		}
		
		if (result[i] != INVALID_GATE)
			continue;

		offset = l2_ib_to_offset(l2tbl, idx2[i], 0);
		for (j = 0; j < l2tbl->bucket; j++) {
			if ((tbl[offset].reserved & RESERVED_OCCUPIED_BIT) &&
			    addr[i] == tbl[offset].addr) {
				result[i] = tbl[offset].gate;
				break;
			}

			offset++;
		}
	}
}

static int l2_flush(struct l2_table *l2tbl)
{
	if (NULL == l2tbl)
		return -EINVAL;
	if (NULL == l2tbl->table)
		return -EINVAL;
	
	memset(l2tbl->table,
	       0,
	       sizeof(struct l2_entry) * l2tbl->size * l2tbl->bucket);

	return 0;
}


static uint64_t l2_addr_to_u64(char* addr)
{
	uint64_t *addrp = (uint64_t*)addr;

	return  (*addrp & 0x0000FFffFFffFFfflu);
}


/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

static void l2_forward_init_test()
{
	int ret;
	struct l2_table l2tbl;

	ret = l2_init(&l2tbl, 0, 0);
	assert(ret < 0);

	ret = l2_init(&l2tbl, 4, 0);
	assert(ret < 0);

	ret = l2_init(&l2tbl, 0, 2);
	assert(ret < 0);

	ret = l2_init(&l2tbl, 4, 2);
	assert(!ret);
	ret = l2_deinit(&l2tbl);
	assert(!ret);

	ret = l2_init(&l2tbl, 4, 4);
	assert(!ret);
	ret = l2_deinit(&l2tbl);
	assert(!ret);

	ret = l2_init(&l2tbl, 4, 8);
	assert(ret < 0);

	ret = l2_init(&l2tbl, 6, 4);
	assert(ret < 0);
	
	ret = l2_init(&l2tbl, 2<<10, 2);
	assert(!ret);
	ret = l2_deinit(&l2tbl);
	assert(!ret);

	ret = l2_init(&l2tbl, 2<<10, 3);
	assert(ret < 0);
}

static void l2_forward_entry_test()
{
	int ret;
	struct l2_table l2tbl;

	uint64_t addr1 = 0x0123456701234567;
	uint64_t addr2 = 0x9876543210987654;
	uint32_t offset;
	uint16_t index1 = 0x0123;
	uint16_t gate_index;

	ret = l2_init(&l2tbl, 4, 4);
	assert(!ret);

	ret = l2_add_entry(&l2tbl, addr1, index1);
	printf("add entry: %lu, index: %hu\n", addr1, index1);
	assert(!ret);

	ret = l2_find(&l2tbl, addr1, &gate_index, &offset);
	printf("find entry: %lu, index: %hu\n", addr1, gate_index);	
	assert(!ret);
	assert(index1==gate_index);

	ret = l2_find(&l2tbl, addr2, &gate_index, &offset);
	assert(ret < 0);

	ret = l2_del_entry(&l2tbl, addr1);
	assert(!ret);

	ret = l2_del_entry(&l2tbl, addr2);
	assert(ret < 0);

	ret = l2_find(&l2tbl, addr1, &gate_index, &offset);
	assert(ret < 0);
	
	ret = l2_deinit(&l2tbl);
	assert(!ret);	
}

void l2_forward_flush_test()
{
	int ret;
	struct l2_table l2tbl;

	uint64_t addr1 = 0x0123456701234567;
	uint16_t index1 = 0x0123;
	uint16_t gate_index;
	uint32_t offset;

	ret = l2_init(&l2tbl, 4, 4);
	assert(!ret);

	ret = l2_add_entry(&l2tbl, addr1, index1);
	assert(!ret);

	ret = l2_flush(&l2tbl);
	assert(!ret);

	ret = l2_find(&l2tbl, addr1, &gate_index, &offset);
	assert(ret < 0);

	ret = l2_deinit(&l2tbl);
	assert(!ret);	
}

void l2_forward_collision_test()
{
#define H_SIZE 4
#define B_SIZE 4
#define MAX_HB_CNT ((H_SIZE) * (B_SIZE))

	int ret;
	int i;
	struct l2_table l2tbl;

	uint64_t addr[MAX_HB_CNT];
	uint16_t idx[MAX_HB_CNT];
	int success[MAX_HB_CNT];
	uint32_t offset;

	ret = l2_init(&l2tbl, H_SIZE, B_SIZE);
	assert(!ret);

	/* collision happens */
	for (i = 0; i < MAX_HB_CNT; i++) {
		addr[i] = random() % ULONG_MAX;
		idx[i] = random() % USHRT_MAX;

		ret = l2_add_entry(&l2tbl, addr[i], idx[i]);
		printf("insert result:%ld %d %d\n", addr[i], idx[i], ret);
		if (ret < 0)
			success[i] = 0;
		else
			success[i] = 1;
	}

	/* collision happens */
	for (i = 0; i < MAX_HB_CNT; i++) {
		uint16_t gate_index;
		gate_index = 0;
		offset = 0;
		
		ret = l2_find(&l2tbl, addr[i], &gate_index, &offset);
		
		printf("find result: %ld, %d, %d\n",
		       addr[i], gate_index, offset);
		
		if (success[i]) {
			assert(!ret);
			assert(idx[i] == gate_index);
		} else {
			assert(ret);
		}
	}

	ret = l2_deinit(&l2tbl);
	assert(!ret);	
#undef H_SIZE
#undef B_SIZE
#undef MAX_HB_CNT
}

int test_all()
{
	l2_forward_init_test();
	l2_forward_entry_test();
	l2_forward_flush_test();
	l2_forward_collision_test();

	return 0;
}

/******************************************************************************/

struct l2_forward_priv {
	int init;
	struct l2_table l2_table;
	uint16_t default_gate;
};

#define KEY_CMD     "cmd"
#define KEY_ADDR    "addr"
#define KEY_GATE    "gate"

struct l2_forward_query
{
	char *cmd;
	char *str_addr;
	char addr[6];
	gate_t  gate;
};

static struct snobj *init(struct module *m, struct snobj *arg)
{
	struct l2_forward_priv *priv = get_priv(m);
	int ret = 0;
	int size = snobj_eval_int(arg, "size");
	int bucket = snobj_eval_int(arg, "bucket");

	priv->init = 0;
	
	if (size == 0)
		size = MAX_TABLE_SIZE;
	if (bucket == 0)
		bucket = MAX_BUCKET_SIZE;

	assert(priv != NULL);	
	ret = l2_init(&priv->l2_table, size, bucket);

	if (ret != 0) {
		return snobj_err(-ret,
				 "initialization failed with argument " \
                                 "size: '%d' bucket: '%d'\n",
				 size, bucket);
	}

	priv->init = 1;
	return NULL;
}

static void deinit(struct module *m)
{
	struct l2_forward_priv *priv = get_priv(m);

	if (priv->init) {
		priv->init = 0;
		l2_deinit(&priv->l2_table);
	}
}

static struct snobj* parse_query(struct snobj *q,
				 struct l2_forward_query *query)
{
	struct snobj *ret = NULL;
	
	/* parse CMD */
	query->cmd = snobj_eval_str(q, KEY_CMD);

	if (query->cmd == NULL) {
		return snobj_err(EINVAL,
				 "Field '%s' must be given as a string",
				 KEY_CMD);
	}

	/* parse MAC ADDR */
	query->str_addr = snobj_eval_str(q, KEY_ADDR);
	
	if (query->str_addr != NULL) {
		char *addr = query->addr;
		int r = sscanf(query->str_addr,
			       "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
			       addr,
			       addr+1,
			       addr+2,
			       addr+3,
			       addr+4,
			       addr+5);
		
		if (r != 6) {
			return snobj_err(EINVAL,
					 "Invalid MAC address '%s'",
					 query->str_addr);
		}
	}

	/* parse gate */
	query->gate = snobj_eval_int(q, KEY_GATE);
	
	return ret;
}

static struct snobj *query(struct module *m, struct snobj *q)
{
	struct l2_forward_priv *priv = get_priv(m);

	struct l2_forward_query query;

	struct snobj *ret = NULL;

	ret = parse_query(q, &query);

	if (ret != NULL)
		return ret;
	
	if (strcmp(query.cmd, "add") == 0) {
		if (query.str_addr == NULL) {
			return snobj_err(EINVAL,
					 "Field '%s' must be provided for add",
					 KEY_ADDR);	
		}
		
		int r = l2_add_entry(&priv->l2_table,
				   l2_addr_to_u64(query.addr), query.gate);

		if (r == -EEXIST) {
			return snobj_err(EEXIST,
					 "MAC address '%s' already exist",
					 query.str_addr);
		} else if (r == -ENOMEM) {
			return snobj_err(ENOMEM,
					 "Not enough space");
		} else if (r != 0) {
			return snobj_err(EINVAL,
					 "Unknown Erorr: %d\n", r);
		}
	} else if (strcmp(query.cmd, "del") == 0) {
		if (query.str_addr == NULL) {
			return snobj_err(EINVAL,
					 "Field '%s' must be provided for add",
					 KEY_ADDR);	
		}

		int r = l2_del_entry(&priv->l2_table,
				     l2_addr_to_u64(query.addr));

		if (r == -ENOENT) {
			return snobj_err(ENOENT,
					 "MAC address '%s' does not exist",
					 query.str_addr);
		} else if (r != 0) {
			return snobj_err(EINVAL,
					 "Unknown Error: %d\n", r);
		}
	} else if (strcmp(query.cmd, "flush") == 0) {
	} else if (strcmp(query.cmd, "lookup") == 0) {
		if (query.str_addr == NULL) {
			return snobj_err(EINVAL,
					 "Field '%s' must be provided for lookup",
					 KEY_ADDR);
		}

		gate_t gate;
		uint32_t offset;
		int r = l2_find(&priv->l2_table,
				l2_addr_to_u64(query.addr),
				&gate,
				&offset);

		if (r == -ENOENT) {
			return snobj_err(ENOENT,
					 "MAC address '%s' does not exist",
					 query.str_addr);			
		} else if ( r != 0) {
			return snobj_err(EINVAL,
					 "Unknown Error: %d\n", r);
		} else {
			return snobj_str_fmt("Addr: '%s' Gate: '%d' ",
					     query.str_addr,
					     gate);
		}
	} else if (strcmp(query.cmd, "test") == 0) {
		test_all();
	} else if (strcmp(query.cmd, "set-default-gate") == 0) {
	} else {
		return snobj_err(EINVAL,
				 "Invalid cmd '%s'",
				 query.cmd);
	}
	return NULL;
}

static struct snobj *get_desc(const struct module *m)
{
	return NULL;
}

__attribute__((optimize("unroll-loops")))
static void process_batch(struct module *m, struct pkt_batch *batch)
{
	gate_t ogates[MAX_PKT_BURST];
	int r, i = 0;

	uint32_t offset;
	struct l2_forward_priv *priv = get_priv(m);

#if 1
	while (batch->cnt - i >= MAX_LOOKUP_BATCH) {
		uint64_t addrs[MAX_LOOKUP_BATCH];

		for (int j = 0; j < MAX_LOOKUP_BATCH; j++)
			addrs[j] = l2_addr_to_u64(snb_head_data(batch->pkts[i + j]));

		l2_find_batch(&priv->l2_table,
			      addrs,
			      ogates + i,
			      MAX_LOOKUP_BATCH);

		i += MAX_LOOKUP_BATCH;
	}
#endif
	for (; i < batch->cnt; i++) {
		struct snbuf *snb = batch->pkts[i];

		ogates[i] = INVALID_GATE;

		r = l2_find(&priv->l2_table,
			    l2_addr_to_u64(snb_head_data(snb)),
			    &ogates[i],
			    &offset);
	}

	run_split(m, ogates, batch);
}


static const struct mclass l2_forward = {
	.name            = "L2Forward",
	.def_module_name = "l2_forward",
	.priv_size       = sizeof(struct l2_forward_priv),
	.init            = init,
	.deinit          = deinit,
	.query           = query,
	.get_desc        = get_desc,
	.process_batch   = process_batch,
};

ADD_MCLASS(l2_forward)
