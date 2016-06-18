#ifndef _MASTER_H_
#define _MASTER_H_

#include <stdint.h>

#include <arpa/inet.h>

#include "utils/cdlist.h"

/* the protocol is simple for both requests and responses:
 * 4-byte length, followed by a message (encoded 'message_t') */

struct client {
	int fd;
	struct sockaddr_in addr;

	/* buf                                               buf+buf_size
	 * [*********************                 |          )
	 *     (sent/received)   buf+offset       buf+msg_len               */
	char *buf;
	size_t buf_size;
	size_t buf_off;

	uint32_t msg_len;
	size_t msg_len_off;

	int holding_lock;	/* 0 or the depth of nested locking */

	struct cdlist_item master_all;
	struct cdlist_item master_lock_waiting;
	struct cdlist_item master_pause_holding;
};

static inline int is_holding_lock(struct client *c)
{
	return !!c->holding_lock;
}

static inline int is_waiting_lock(struct client *c)
{
	return cdlist_is_hooked(&c->master_lock_waiting);
}

static inline int is_holding_pause(struct client *c)
{
	return cdlist_is_hooked(&c->master_pause_holding);
}

void setup_master();

/* The main run loop of the channel thread. Never returns. */
void run_master();

#endif
