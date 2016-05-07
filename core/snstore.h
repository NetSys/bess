#ifndef _SNSTORE_H_
#define _SNSTORE_H_

/* snstore is a simple key-value store that can be accessed across all DPDK 
 * processes. It is currently used specifically for passing native vports.
 * Simple linear search is used, so guranteed to be slow */

#include <string.h>

#include <rte_config.h>
#include <rte_memzone.h>

#define SNSTORE_KEYSIZE	24
#define SNSTORE_PAIRS	128

struct snstore_kvpair {
	char key[SNSTORE_KEYSIZE];
	void *value;
};

/* Must be called by BESS only */
void init_snstore(void);

/* returns -1 if not exists */
static inline void *snstore_get(const char *key)
{
	const struct rte_memzone *memzone;
	struct snstore_kvpair *kvpair;
	int i;

	assert(strlen(key) > 0);

	memzone = rte_memzone_lookup("snstore");
	assert(memzone);

	kvpair = (struct snstore_kvpair *)memzone->addr;

	for (i = 0; i < SNSTORE_PAIRS; i++) {
		if (strncmp(key, kvpair->key, SNSTORE_KEYSIZE) == 0)
			return kvpair->value;

		kvpair++;
	}

	return NULL;
}

/* returns -1 if already exists or no more free slots available */
static inline int snstore_put(const char *key, void *value)
{
	const struct rte_memzone *memzone;
	struct snstore_kvpair *kvpair;
	struct snstore_kvpair *slot = NULL;
	int i;

	assert(strlen(key) > 0);

	memzone = rte_memzone_lookup("snstore");
	assert(memzone);

	kvpair = (struct snstore_kvpair *)memzone->addr;

	for (i = 0; i < SNSTORE_PAIRS; i++) {
		if (strncmp(key, kvpair->key, SNSTORE_KEYSIZE) == 0)
			return -1;

		if (!slot && strcmp("", kvpair->key) == 0)
			slot = kvpair;

		kvpair++;
	}

	if (!slot)
		return -1;
	
	strncpy(slot->key, key, SNSTORE_KEYSIZE);
	slot->key[SNSTORE_KEYSIZE - 1] = '\0';
	slot->value = value;

	return 0;
}

static inline void snstore_del(const char *key)
{
	const struct rte_memzone *memzone;
	struct snstore_kvpair *kvpair;
	int i;

	assert(strlen(key) > 0);

	memzone = rte_memzone_lookup("snstore");
	assert(memzone);

	kvpair = (struct snstore_kvpair *)memzone->addr;

	for (i = 0; i < SNSTORE_PAIRS; i++) {
		if (strncmp(key, kvpair->key, SNSTORE_KEYSIZE) == 0) {
			strcpy(kvpair->key, "");
			return;
		}

		kvpair++;
	}
}
#endif
