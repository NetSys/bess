#include <assert.h>
#include <stdio.h>

#include "snstore.h"

void init_snstore(void)
{
	const struct rte_memzone *memzone;
	struct snstore_kvpair *kvpair;
	int i;

	memzone = rte_memzone_reserve("snstore", 
			sizeof(struct snstore_kvpair) * SNSTORE_PAIRS,
			SOCKET_ID_ANY, 
			RTE_MEMZONE_2MB | RTE_MEMZONE_SIZE_HINT_ONLY);
	assert(memzone);
	
	kvpair = (struct snstore_kvpair *)memzone->addr;

	for (i = 0; i < SNSTORE_PAIRS; i++) {
		strcpy(kvpair->key, "");
		kvpair++;
	}
}
