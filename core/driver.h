#ifndef _DRIVER_H_ 
#define _DRIVER_H_

#include "common.h"

#include "snbuf.h"

#define ADD_DRIVER(drv) \
	__attribute__((constructor(102))) void __driver_register_##drv() \
	{ \
		int ret; \
		ret = add_driver(&drv); \
		assert(ret == 0); \
	}

struct port;
struct port_stats;
struct snobj;

struct driver {
	/* Required: should be like "CamelCase" */
	const char *name;

	/* Optional: should be like "lower_case". 
	 * - "%d" is automatically appended.
	 * - Anonymous modules will have a default name "source0", "source1", ... 
	 * - If this field is not provided, the mclass name will be used 
	 *   after auto transformation (CamelCase -> camel_case)*/
	const char *def_port_name;

	/* Optional: the size of per-port private data, if any. 0 by default */
	size_t priv_size;

	/* Optional */
	int def_size_inc_q;
	int def_size_out_q;

	/* Optional */
	int (*init_driver)(struct driver *driver);

	/* Required */
	struct snobj *(*init_port)(struct port *p, struct snobj *conf);

	/* Optional: cleanup internal state */
	void (*deinit_port)(struct port *p);

	/* Optional: port-specific query interface */
	struct snobj *(*query)(struct port *p, struct snobj *q);
	
	/* Optional */
	int (*recv_pkts)(struct port *p, queue_t qid, snb_array_t pkts, int cnt);

	/* Optional */
	int (*send_pkts)(struct port *p, queue_t qid, snb_array_t pkts, int cnt);
};

size_t list_drivers(const struct driver **p_arr, size_t arr_size, size_t offset);

const struct driver *find_driver(const char *name);

int add_driver(const struct driver *driver);

void init_drivers();

#endif
