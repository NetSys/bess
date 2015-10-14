#ifndef _PORT_H_
#define _PORT_H_

#include <stdint.h>

#include "common.h"
#include "snobj.h"
#include "driver.h"

#if MAX_QUEUES_PER_PORT_DIR >= QUEUE_UNKNOWN ||  MAX_QUEUES > 255
	#error Invalid macro constants
#endif

#define PORT_NAME_LEN		128

struct module;

struct packet_stats {
	uint64_t packets;
	uint64_t dropped;	/* Not all drivers support this for inc dir */
	uint64_t bytes;		/* doesn't include Ethernet overhead */
};

typedef struct packet_stats port_stats_t[PACKET_DIRS];

struct port {
	char *name;

	const struct driver *driver;

	/* how many modules are using this port?
	 * TODO: more robust gate keeping */
	int users;

	char mac_addr[ETH_ALEN];

	int num_queues[PACKET_DIRS];

	struct packet_stats queue_stats[PACKET_DIRS][MAX_QUEUES_PER_DIR];
	
	void *priv[0];	
};

static inline void *get_port_priv(struct port *p) {
	return (void *)(p + 1);
}

size_t list_ports(const struct port **p_arr, size_t arr_size, size_t offset);
struct port *find_port(const char *name);

struct port *create_port(const char *name,
		const struct driver *driver, 
		struct snobj *arg,
		struct snobj **perr);

int destroy_port(struct port *p);

void get_port_stats(struct port *p, port_stats_t *stats);

void get_queue_stats(struct port *p, packet_dir_t dir, queue_t qid, 
		struct packet_stats *stats);

void acquire_queue(struct port *p, packet_dir_t dir, queue_t qid, 
		struct module *m);
void release_queue(struct port *p, packet_dir_t dir, queue_t qid, 
		struct module *m);

#endif
