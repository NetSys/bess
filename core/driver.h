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

typedef uint8_t queue_t;

#define QUEUE_UNKNOWN			255
#define MAX_QUEUES_PER_DIR		32	/* [0, 31] (for each RX/TX) */

ct_assert(MAX_QUEUES_PER_DIR < QUEUE_UNKNOWN);

struct port;
struct port_stats;
struct snobj;

typedef int (*pkt_io_func_t)(struct port *, queue_t, snb_array_t, int);

#define DRIVER_FLAG_SELF_INC_STATS	0x0001
#define DRIVER_FLAG_SELF_OUT_STATS	0x0002

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

	/* Optional. In number of packets */
	size_t def_size_inc_q;
	size_t def_size_out_q;

	/* Optional. 0 is good for most drivers */
	uint32_t flags;

	/* Optional */
	int (*init_driver)(struct driver *driver);

	/* Required */
	struct snobj *(*init_port)(struct port *p, struct snobj *conf);

	/* Optional: cleanup internal state */
	void (*deinit_port)(struct port *p);

	/* Optional: collect internal (HW) stats, if available */
	void (*collect_stats)(struct port *p, int reset);

	/* Optional: port-specific query interface */
	struct snobj *(*query)(struct port *p, struct snobj *q);
	
	/* Optional */
	pkt_io_func_t recv_pkts;

	/* Optional */
	pkt_io_func_t send_pkts;
};

size_t list_drivers(const struct driver **p_arr, size_t arr_size, size_t offset);

const struct driver *find_driver(const char *name);

int add_driver(const struct driver *driver);

void init_drivers();

#endif
