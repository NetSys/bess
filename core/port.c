#include <errno.h>

#include <rte_malloc.h>

#include "port.h"
#include "driver.h"
#include "namespace.h"

size_t list_ports(const struct port **p_arr, size_t arr_size, size_t offset)
{
	int ret = 0;
	int iter_cnt = 0;

	struct ns_iter iter;

	ns_init_iterator(&iter, NS_TYPE_PORT);
	while(1) {
		struct port *port = (struct port*) ns_next(&iter);
		if (!port)
			break;

		if (iter_cnt++ < offset)
			continue;

		if (ret >= arr_size)
			break;

		p_arr[ret++] = port;
	}
	ns_release_iterator(&iter);

	return ret;
}

static void set_default_name(struct port *p)
{
	char *fmt;

	int i;

	if (p->driver->def_port_name) {
		fmt = alloca(strlen(p->driver->def_port_name) + 16);
		strcpy(fmt, p->driver->def_port_name);
	} else {
		const char *template = p->driver->name;
		const char *t;

		char *s;

		fmt = alloca(strlen(template) + 16);
		s = fmt;

		/* CamelCase -> camel_case */
		for (t = template; *t != '\0'; t++) {
			if (t != template && islower(*(t - 1)) && isupper(*t))
				*s++ = '_';
			
			*s++ = tolower(*t);
		}

		*s = '\0';
	}

	/* lower_case -> lower_case%d */
	strcat(fmt, "%d");

	for (i = 0; ; i++) {
		int ret;

		ret = snprintf(p->name, PORT_NAME_LEN, fmt, i);
		assert(ret < PORT_NAME_LEN);

		if (!find_port(p->name))
			break;	/* found an unallocated name! */
	}
}

/* currently very slow with linear search */
struct port *find_port(const char *name)
{
	return (struct port *) ns_lookup(NS_TYPE_PORT, name);
}

static int register_port(struct port *p)
{
	int ret;

	ret = ns_insert(NS_TYPE_PORT, p->name, (void *) p);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

/* returns a pointer to the created port.
 * if error, returns NULL and *perr is set */
struct port *create_port(const char *name, 
		const struct driver *driver, 
		struct snobj *arg,
		struct snobj **perr)
{
	struct port *p = NULL;
	int ret;

	int num_inc_q = 1;
	int num_out_q = 1;
	uint8_t mac_addr[ETH_ALEN];

	*perr = NULL;

	if (snobj_eval_exists(arg, "num_inc_q"))
		num_inc_q = snobj_eval_int(arg, "num_inc_q");

	if (snobj_eval_exists(arg, "num_out_q"))
		num_out_q = snobj_eval_int(arg, "num_out_q");

	if (snobj_eval_exists(arg, "mac_addr")) {
		char *v = snobj_eval_str(arg, "mac_addr");
		
		ret = sscanf(v, 
				"%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
				&mac_addr[0],
				&mac_addr[1],
				&mac_addr[2],
				&mac_addr[3],
				&mac_addr[4],
				&mac_addr[5]);

		if (ret != 6) {
			*perr = snobj_err(EINVAL, "MAC address should be " \
					"formatted as a string " \
					"xx:xx:xx:xx:xx:xx");
			goto fail;
		}
	} else
		eth_random_addr(mac_addr);

	if (num_inc_q < 0 || num_inc_q > MAX_QUEUES_PER_DIR ||
	    num_out_q < 0 || num_out_q > MAX_QUEUES_PER_DIR) {
		*perr = snobj_err(EINVAL, "Invalid number of queues");
		goto fail;
	}

	if (name && find_port(name)) {
		*perr = snobj_err(EEXIST, "Port '%s' already exists");
		goto fail;
	}

	p = rte_zmalloc("port", sizeof(struct port) + driver->priv_size , 0);
	if (!p) {
		*perr = snobj_errno(ENOMEM);
		goto fail;
	}

	p->name = rte_zmalloc("name", PORT_NAME_LEN, 0);
	if (!p->name) {
		*perr = snobj_errno(ENOMEM);
		goto fail;
	}

	p->driver = driver;

	memcpy(p->mac_addr, mac_addr, ETH_ALEN);
	p->num_queues[PACKET_DIR_INC] = num_inc_q;
	p->num_queues[PACKET_DIR_OUT] = num_out_q;

	if (!name)
		set_default_name(p);
	else
		snprintf(p->name, PORT_NAME_LEN, "%s", name);

	*perr = p->driver->init_port(p, arg);
	if (*perr != NULL)
		goto fail;

	ret = register_port(p);
	if (ret != 0) {
		*perr = snobj_errno(-ret);
		goto fail;
	}

	return p;

fail:
	if (p)
		rte_free(p->name);

	rte_free(p);

	return NULL;
}

/* -errno if not successful */
int destroy_port(struct port *p)
{
	int ret;

	if (p->users)
		return -EBUSY;
	
	ret = ns_remove(p->name);
	if (ret < 0) {
		return ret; 
	}

	if (p->driver->deinit_port)
		p->driver->deinit_port(p);

	rte_free(p->name);
	rte_free(p);

	return 0;
}

void get_port_stats(struct port *p, port_stats_t *stats)
{
	for (packet_dir_t dir = 0; dir < PACKET_DIRS; dir++) {
		memset(&((*stats)[dir]), 0, sizeof(struct packet_stats));

		for (queue_t qid = 0; qid < p->num_queues[dir]; qid++) {
			const struct packet_stats *queue_stats = &p->queue_stats[dir][qid];

			(*stats)[dir].packets	+= queue_stats->packets;
			(*stats)[dir].dropped	+= queue_stats->dropped;
			(*stats)[dir].bytes 	+= queue_stats->bytes;
		}
	}
}

void get_queue_stats(struct port *p, packet_dir_t dir, queue_t qid, 
		struct packet_stats *stats)
{
	memcpy(stats, &p->queue_stats[dir][qid], sizeof(*stats));
}

/* TODO: you can do better than this */
void acquire_queue(struct port *p, packet_dir_t dir, queue_t qid, 
		struct module *m)
{
	p->users++;
}

void release_queue(struct port *p, packet_dir_t dir, queue_t qid, 
		struct module *m)
{
	p->users--;
}
