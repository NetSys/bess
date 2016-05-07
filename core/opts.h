#ifndef _OPTS_H_
#define _OPTS_H_

#include <stdint.h>

extern const struct global_opts {
	uint16_t port;		/* TCP port for controller (0 for default) */
	int default_core;	/* Core ID for implicily launched worker */
	int foreground;		/* If 1, not daemonized */
	int kill_existing;	/* If 1, kill existing BESS instance */
	int print_tc_stats;	/* If 1, print TC stats every second */
	int debug_mode;		/* If 1, print control messages */
	int mb_per_socket;	/* MB per CPU socket for DPDK (0=default) */
	char *pidfile;		/* Filename (nullptr=default; nullstr=none) */
	int multi_instance;	/* If 1, allow multiple BESS instances */
} global_opts;

#endif
