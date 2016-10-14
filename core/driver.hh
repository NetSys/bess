#ifndef _DRIVER_H_
#define _DRIVER_H_

#include <string>

#include "common.h"
#include "namespace.h"
#include "log.h"

typedef uint8_t queue_t;

#define QUEUE_UNKNOWN			255
#define MAX_QUEUES_PER_DIR		32	/* [0, 31] (for each RX/TX) */

ct_assert(MAX_QUEUES_PER_DIR < QUEUE_UNKNOWN);

#define DRIVER_FLAG_SELF_INC_STATS	0x0001
#define DRIVER_FLAG_SELF_OUT_STATS	0x0002

#define MAX_COMMANDS		32

struct port;
struct port_stats;
struct snobj;

typedef struct snbuf * restrict * restrict snb_array_t;

typedef int (*pkt_io_func_t)(struct port *, queue_t, snb_array_t, int);

#if 0
typedef struct snobj *
(*port_cmd_func_t) (struct port *, const char *, struct snobj *);

Driver {
	/* Required: should be like "CamelCase" */
	const char *name;

	/* Optional: one-line description of the port driver */
	const char *help;

	/* Optional: should be like "lower_case".
	 * - "%d" is automatically appended.
	 * - Unnamed ports will have a default name "pmd_port0", "pmd_port1" ...
	 * - If this field is not provided, the driver name will be used
	 *   after auto transformation (CamelCase -> camel_case) */
	const char *def_port_name;

	/* Optional: the size of per-port private data, if any. 0 by default */
	size_t priv_size;

	/* Optional. In number of packets */
	size_t def_size_inc_q;
	size_t def_size_out_q;

	/* Optional. 0 is good for most drivers */
	uint32_t flags;

	/* Optional */
	int (*init_driver)(Driver *driver);

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

	const struct {
		const char *cmd;
		port_cmd_func_t func;

		/* if non-zero, workers don't need to be paused in order to
		 * run this command */
		int mt_safe;
	} commands[MAX_COMMANDS];
};
#endif


#endif
