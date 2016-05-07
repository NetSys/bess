#ifndef _PORT_H_
#define _PORT_H_

#include <stdint.h>

#include "common.h"
#include "log.h"
#include "snobj.h"
#include "driver.h"

#define PORT_NAME_LEN		128

#define DEFAULT_QUEUE_SIZE	256
#define MAX_QUEUE_SIZE		4096

/* The term RX/TX could be very confusing for a virtual switch.
 * Instead, we use the "incoming/outgoing" convention:
 * - incoming: outside -> BESS
 * - outgoing: BESS -> outside */
typedef enum {
	PACKET_DIR_INC 	= 0,
	PACKET_DIR_OUT 	= 1,
	PACKET_DIRS
} packet_dir_t;

struct packet_stats {
	uint64_t packets;
	uint64_t dropped;	/* Not all drivers support this for inc dir */
	uint64_t bytes;		/* doesn't include Ethernet overhead */
};

typedef struct packet_stats port_stats_t[PACKET_DIRS];

struct module;

struct port {
	char *name;

	const struct driver *driver;

	/* which modules are using this port?
	 * TODO: more robust gate keeping */
	const struct module *users[PACKET_DIRS][MAX_QUEUES_PER_DIR];

	char mac_addr[ETH_ALEN];

	queue_t num_queues[PACKET_DIRS];
	int queue_size[PACKET_DIRS];

	struct packet_stats queue_stats[PACKET_DIRS][MAX_QUEUES_PER_DIR];
	
	/* for stats that do NOT belong to any queues */
	port_stats_t port_stats;	

	void *priv[0];	
};

static inline void *get_port_priv(struct port *p) 
{
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

/* quques == NULL if _all_ queues are being acquired/released */
int acquire_queues(struct port *p, const struct module *m, packet_dir_t dir, 
		const queue_t *queues, int num_queues);
void release_queues(struct port *p, const struct module *m, packet_dir_t dir, 
		const queue_t *queues, int num_queues);

#endif
