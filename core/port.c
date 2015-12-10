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

	queue_t num_inc_q = 1;
	queue_t num_out_q = 1;
	size_t size_inc_q = driver->def_size_inc_q ? : DEFAULT_QUEUE_SIZE;
	size_t size_out_q = driver->def_size_out_q ? : DEFAULT_QUEUE_SIZE;

	uint8_t mac_addr[ETH_ALEN];

	*perr = NULL;

	if (snobj_eval_exists(arg, "num_inc_q"))
		num_inc_q = snobj_eval_uint(arg, "num_inc_q");

	if (snobj_eval_exists(arg, "num_out_q"))
		num_out_q = snobj_eval_uint(arg, "num_out_q");

	if (snobj_eval_exists(arg, "size_inc_q"))
		size_inc_q = snobj_eval_uint(arg, "size_inc_q");

	if (snobj_eval_exists(arg, "size_out_q"))
		size_out_q = snobj_eval_uint(arg, "size_out_q");

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

	if (num_inc_q > MAX_QUEUES_PER_DIR || num_out_q > MAX_QUEUES_PER_DIR) {
		*perr = snobj_err(EINVAL, "Invalid number of queues");
		goto fail;
	}

	if (num_inc_q > 0 && !driver->recv_pkts) {
		*perr = snobj_err(EINVAL, "Driver '%s' does not support "
				"packet reception", driver->name);
		goto fail;
	}

	if (num_out_q > 0 && !driver->send_pkts) {
		*perr = snobj_err(EINVAL, "Driver '%s' does not support "
				"packet transmission", driver->name);
		goto fail;
	}

	if (size_inc_q < 0 || size_inc_q > MAX_QUEUE_SIZE ||
	    size_out_q < 0 || size_out_q > MAX_QUEUE_SIZE) {
		*perr = snobj_err(EINVAL, "Invalid queue size");
		goto fail;
	}

	if (name && find_port(name)) {
		*perr = snobj_err(EEXIST, "Port '%s' already exists", name);
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

	p->queue_size[PACKET_DIR_INC] = size_inc_q;
	p->queue_size[PACKET_DIR_OUT] = size_out_q;

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

	for (packet_dir_t dir = 0; dir < PACKET_DIRS; dir++) {
		for (int i = 0; i < p->num_queues[dir]; i++) 
			if (p->users[dir][i])
				return -EBUSY;
	}
	
	ret = ns_remove(p->name);
	if (ret < 0)
		return ret; 

	if (p->driver->deinit_port)
		p->driver->deinit_port(p);

	rte_free(p->name);
	rte_free(p);

	return 0;
}

void get_port_stats(struct port *p, port_stats_t *stats)
{
	if (p->driver->collect_stats)
		p->driver->collect_stats(p, 0);

	memcpy(stats, &p->port_stats, sizeof(port_stats_t));

	for (packet_dir_t dir = 0; dir < PACKET_DIRS; dir++) {
		for (queue_t qid = 0; qid < p->num_queues[dir]; qid++) {
			const struct packet_stats *queue_stats;
			
			queue_stats = &p->queue_stats[dir][qid];

			(*stats)[dir].packets	+= queue_stats->packets;
			(*stats)[dir].dropped	+= queue_stats->dropped;
			(*stats)[dir].bytes 	+= queue_stats->bytes;
		}
	}
}

/* XXX: Do we need this? Currently not being used anywhere */
void get_queue_stats(struct port *p, packet_dir_t dir, queue_t qid, 
		struct packet_stats *stats)
{
	memcpy(stats, &p->queue_stats[dir][qid], sizeof(*stats));
}

int acquire_queues(struct port *p, const struct module *m, packet_dir_t dir, 
		const queue_t *queues, int num_queues)
{
	queue_t qid;
	int i;

	if (dir != PACKET_DIR_INC && dir != PACKET_DIR_OUT) {
		fprintf(stderr, "Incorrect packet dir %d\n", dir);
		return -EINVAL;
	}

	if (queues == NULL) {
		for (qid = 0; qid < p->num_queues[dir]; qid++) {
			const struct module *user;

			user = p->users[dir][qid];

			/* the queue is already being used by someone else? */
			if (user && user != m)
				return -EBUSY;
		}

		for (qid = 0; qid < p->num_queues[dir]; qid++)
			p->users[dir][qid] = m;

		return 0;
	}

	for (i = 0; i < num_queues; i++) {
		const struct module *user;
		
		qid = queues[i];
	
		if (qid >= p->num_queues[dir])
			return -EINVAL;

		user = p->users[dir][qid];

		/* the queue is already being used by someone else? */
		if (user && user != m)
			return -EBUSY;
	}

	for (i = 0; i < num_queues; i++) {
		qid = queues[i];
		p->users[dir][qid] = m;
	}

	return 0;
}

void release_queues(struct port *p, const struct module *m, packet_dir_t dir, 
		const queue_t *queues, int num_queues)
{
	queue_t qid;
	int i;

	if (dir != PACKET_DIR_INC && dir != PACKET_DIR_OUT) {
		fprintf(stderr, "Incorrect packet dir %d\n", dir);
		return;
	}

	if (queues == NULL) {
		for (qid = 0; qid < p->num_queues[dir]; qid++) {
			if (p->users[dir][qid] == m)
				p->users[dir][qid] = NULL;
		}

		return;
	}

	for (i = 0; i < num_queues; i++) {
		qid = queues[i];
		if (qid >= p->num_queues[dir])
			continue;

		if (p->users[dir][qid] == m)
			p->users[dir][qid] = NULL;
	}
}
