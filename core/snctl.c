#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include <sys/time.h>
#include <sys/types.h>

#include "opts.h"
#include "worker.h"
#include "master.h"
#include "snobj.h"
#include "module.h"
#include "port.h"
#include "time.h"

struct handler_map {
	const char *cmd;
	int pause_needed;	/* should all workers have been paused? */
	struct snobj *(*func)(struct snobj *);
};

static const char *resource_names[NUM_RESOURCES] = 
		{"schedules", "cycles", "packets", "bits"};

static int name_to_resource(const char *name)
{
	for (int i = 0; i < NUM_RESOURCES; i++)
		if (strcmp(resource_names[i], name) == 0)
			return i;

	/* not found */
	return -1;
}

static struct snobj *handle_reset_modules(struct snobj *);
static struct snobj *handle_reset_ports(struct snobj *);
static struct snobj *handle_reset_tcs(struct snobj *);
static struct snobj *handle_reset_workers(struct snobj *);

static struct snobj *handle_reset_all(struct snobj *q)
{
	struct snobj *r;

	log_info("*** reset_all requested ***\n");

	r = handle_reset_modules(NULL);
	if (r)
		return r;

	r = handle_reset_ports(NULL);
	if (r)
		return r;

	r = handle_reset_tcs(NULL);
	if (r)
		return r;

	r = handle_reset_workers(NULL);
	if (r)
		return r;

	return NULL;
}

static struct snobj *handle_pause_all(struct snobj *q)
{
	pause_all_workers();
	log_info("*** All workers have been paused ***\n");
	return NULL;
}

static struct snobj *handle_resume_all(struct snobj *q)
{
	log_info("*** Resuming ***\n");
	resume_all_workers();
	return NULL;
}

static struct snobj *handle_reset_workers(struct snobj *q)
{
	destroy_all_workers();
	log_info("*** All workers have been destroyed ***\n");
	return NULL;
}

static struct snobj *handle_list_workers(struct snobj *q)
{
	struct snobj *r;

	r = snobj_list();

	for (int wid = 0; wid < MAX_WORKERS; wid++) {
		struct snobj *worker;

		if (!is_worker_active(wid))
			continue;

		worker = snobj_map();
		snobj_map_set(worker, "wid", snobj_int(wid));
		snobj_map_set(worker, "running", 
				snobj_int(is_worker_running(wid)));
		snobj_map_set(worker, "core",
				snobj_int(workers[wid]->core));
		snobj_map_set(worker, "num_tcs",
				snobj_int(workers[wid]->s->num_classes));
		snobj_map_set(worker, "silent_drops",
				snobj_int(workers[wid]->silent_drops));

		snobj_list_add(r, worker);
	}

	return r;
}

static struct snobj *handle_add_worker(struct snobj *q)
{
	unsigned int wid;
	unsigned int core;

	struct snobj *t;

	t = snobj_eval(q, "wid");
	if (!t)
		return snobj_err(EINVAL, "Missing 'wid' field");

	wid = snobj_uint_get(t);
	if (wid >= MAX_WORKERS)
		return snobj_err(EINVAL, "'wid' must be between 0 and %d",
				MAX_WORKERS - 1);

	t = snobj_eval(q, "core");
	if (!t)
		return snobj_err(EINVAL, "Missing 'core' field");

	core = snobj_uint_get(t);
	if (!is_cpu_present(core))
		return snobj_err(EINVAL, "Invalid core %d", core);

	if (is_worker_active(wid))
		return snobj_err(EEXIST, "worker:%d is already active", wid);

	launch_worker(wid, core);

	return NULL;
}

static struct snobj *handle_reset_tcs(struct snobj *q)
{
	struct ns_iter iter;
	struct tc *c;

	struct tc **c_arr;
	size_t arr_slots = 1024;
	int n = 0;

	c_arr = malloc(arr_slots * sizeof(struct tc *));

	ns_init_iterator(&iter, NS_TYPE_TC);

	while ((c = (struct tc *)ns_next(&iter)) != NULL) {
		if (n >= arr_slots) {
			arr_slots *= 2;
			c_arr = realloc(c_arr, arr_slots * sizeof(struct tc *));
		}

		c_arr[n] = c;
		n++;
	}

	ns_release_iterator(&iter);	

	for (int i = 0; i < n; i++) {
		c = c_arr[i];

		if (c->num_tasks) {
			free(c_arr);
			return snobj_err(EBUSY, "TC %s still has %d tasks",
					c->settings.name, c->num_tasks);
		}

		if (c->settings.auto_free)
			continue;

		tc_leave(c);
		tc_dec_refcnt(c);
	}

	free(c_arr);
	return NULL;
}

static struct snobj *handle_list_tcs(struct snobj *q)
{
	struct snobj *r;
	struct snobj *t;

	unsigned int wid_filter = MAX_WORKERS;

	struct ns_iter iter;

	struct tc *c;

	t = snobj_eval(q, "wid");
	if (t) {
		wid_filter = snobj_uint_get(t);

		if (wid_filter >= MAX_WORKERS)
			return snobj_err(EINVAL, 
					"'wid' must be between 0 and %d",
					MAX_WORKERS - 1);

		if (!is_worker_active(wid_filter))
			return snobj_err(EINVAL, "worker:%d does not exist", 
					wid_filter);
	}

	r = snobj_list();

	ns_init_iterator(&iter, NS_TYPE_TC);

	while ((c = (struct tc *)ns_next(&iter)) != NULL) {
		int wid;

		if (wid_filter < MAX_WORKERS) {
			if (workers[wid_filter]->s != c->s)
				continue;
			wid = wid_filter;
		} else {
			for (wid = 0; wid < MAX_WORKERS; wid++)
				if (is_worker_active(wid) && 
						workers[wid]->s == c->s)
					break;
		}

		struct snobj *elem = snobj_map();

		snobj_map_set(elem, "name", snobj_str(c->settings.name));
		snobj_map_set(elem, "tasks", snobj_int(c->num_tasks));
		snobj_map_set(elem, "parent", snobj_str(c->parent->settings.name));
		snobj_map_set(elem, "priority", snobj_int(c->settings.priority));

		if (wid < MAX_WORKERS)
			snobj_map_set(elem, "wid", snobj_uint(wid));
		else
			snobj_map_set(elem, "wid", snobj_int(-1));

		struct snobj *limit = snobj_map();

		for (int i = 0; i < NUM_RESOURCES; i++) {
			  snobj_map_set(limit, resource_names[i],
					snobj_uint(c->settings.limit[i]));
		}

		snobj_map_set(elem, "limit", limit);

		struct snobj *max_burst = snobj_map();

		for (int i = 0; i < NUM_RESOURCES; i++) {
			  snobj_map_set(max_burst, resource_names[i],
					snobj_uint(c->settings.max_burst[i]));
		}

		snobj_map_set(elem, "max_burst", max_burst);

		snobj_list_add(r, elem);
	}

	ns_release_iterator(&iter);

	return r;
}

static struct snobj *handle_add_tc(struct snobj *q)
{
	const char *tc_name;
	int wid;

	struct tc_params params;
	struct tc *c;

	tc_name = snobj_eval_str(q, "name");
	if (!tc_name)
		return snobj_err(EINVAL, "Missing 'name' field");

	if (!ns_is_valid_name(tc_name))
		return snobj_err(EINVAL, "'%s' is an invalid name", tc_name);

	if (ns_name_exists(tc_name))
		return snobj_err(EINVAL, "Name '%s' already exists", tc_name);

	wid = snobj_eval_uint(q, "wid");
	if (wid >= MAX_WORKERS)
		return snobj_err(EINVAL, 
				"'wid' must be between 0 and %d",
				MAX_WORKERS - 1);

	if (!is_worker_active(wid)) {
		if (num_workers == 0 && wid == 0)
			launch_worker(wid, global_opts.default_core);
		else
			return snobj_err(EINVAL, 
					"worker:%d does not exist", wid);
	}

	memset(&params, 0, sizeof(params));

	strcpy(params.name, tc_name);

	params.priority = snobj_eval_int(q, "priority");
	if (params.priority == DEFAULT_PRIORITY)
		return snobj_err(EINVAL, "Priority %d is reserved",
				DEFAULT_PRIORITY);

	/* TODO: add support for other parameters */
	params.share = 1;
	params.share_resource = RESOURCE_CNT;

	struct snobj *limit = snobj_eval(q, "limit");
	if (limit) {
		if (snobj_type(limit) != TYPE_MAP)
			return snobj_err(EINVAL, "'limit' must be a map\n");

		for (int i = 0; i < limit->size; i++) {
			int rsc = name_to_resource(limit->map.arr_k[i]);

			if (rsc < 0)
				return snobj_err(EINVAL, 
						"Invalid resource name '%s'\n",
						limit->map.arr_k[i]);

			params.limit[rsc] = snobj_uint_get(limit->map.arr_v[i]);
		}
	}

	struct snobj *max_burst = snobj_eval(q, "max_burst");
	if (max_burst) {
		if (snobj_type(max_burst) != TYPE_MAP)
			return snobj_err(EINVAL, "'max_burst' must be a map\n");

		for (int i = 0; i < max_burst->size; i++) {
			int rsc = name_to_resource(max_burst->map.arr_k[i]);

			if (rsc < 0)
				return snobj_err(EINVAL, 
						"Invalid resource name '%s'\n",
						max_burst->map.arr_k[i]);

			params.max_burst[rsc] = 
				snobj_uint_get(max_burst->map.arr_v[i]);
		}
	}

	c = tc_init(workers[wid]->s, &params);
	if (is_err(c))
		return snobj_err(-ptr_to_err(c), "tc_init() failed");

	tc_join(c);

	return NULL;
}

static struct snobj *handle_get_tc_stats(struct snobj *q)
{
	const char *tc_name;

	struct tc *c;
	
	struct snobj *r;

	tc_name = snobj_str_get(q);
	if (!tc_name)
		return snobj_err(EINVAL, "Argument must be a name in str");

	c = ns_lookup(NS_TYPE_TC, tc_name);
	if (!c)
		return snobj_err(ENOENT, "No TC '%s' found", tc_name);

	r = snobj_map();

	snobj_map_set(r, "timestamp", snobj_double(get_epoch_time()));
	snobj_map_set(r, "count", 
			snobj_uint(c->stats.usage[RESOURCE_CNT]));
	snobj_map_set(r, "cycles", 
			snobj_uint(c->stats.usage[RESOURCE_CYCLE]));
	snobj_map_set(r, "packets", 
			snobj_uint(c->stats.usage[RESOURCE_PACKET]));
	snobj_map_set(r, "bits", 
			snobj_uint(c->stats.usage[RESOURCE_BIT]));

	return r;
}

static struct snobj *handle_list_drivers(struct snobj *q)
{
	struct snobj *r;

	int cnt = 1;
	int offset;

	r = snobj_list();

	for (offset = 0; cnt != 0; offset += cnt) {
		const int arr_size = 16;
		const struct driver *drivers[arr_size];

		int i;

		cnt = list_drivers(drivers, arr_size, offset);

		for (i = 0; i < cnt; i++)
			snobj_list_add(r, snobj_str(drivers[i]->name));
	};

	return r;
}

static struct snobj *handle_reset_ports(struct snobj *q)
{
	struct port *p;

	while (list_ports((const struct port **)&p, 1, 0)) {
		int ret = destroy_port(p);
		if (ret)
			return snobj_errno(-ret);
	}

	log_info("*** All ports have been destroyed ***\n");
	return NULL;
}

static struct snobj *handle_list_ports(struct snobj *q)
{
	struct snobj *r;

	int cnt = 1;
	int offset;

	r = snobj_list();

	for (offset = 0; cnt != 0; offset += cnt) {
		const int arr_size = 16;
		const struct port *ports[arr_size];

		int i;

		cnt = list_ports(ports, arr_size, offset);

		for (i = 0; i < cnt; i++) {
			struct snobj *port = snobj_map();

			snobj_map_set(port, "name",
					snobj_str(ports[i]->name));
			snobj_map_set(port, "driver",
					snobj_str(ports[i]->driver->name));

			snobj_list_add(r, port);
		}
	};

	return r;
}

static struct snobj *handle_create_port(struct snobj *q)
{
	const char *driver_name;
	const struct driver *driver;
	struct port *port;

	struct snobj *r;
	struct snobj *err;

	driver_name = snobj_eval_str(q, "driver");
	if (!driver_name)
		return snobj_err(EINVAL, "Missing 'driver' field");

	driver = find_driver(driver_name);
	if (!driver)
		return snobj_err(ENOENT, "No port driver '%s' found",
				driver_name);

	port = create_port(snobj_eval_str(q, "name"), driver, 
			snobj_eval(q, "arg"), &err);
	if (!port)
		return err;

	r = snobj_map();
	snobj_map_set(r, "name", snobj_str(port->name));

	return r;
}

static struct snobj *handle_destroy_port(struct snobj *q)
{
	const char *port_name;

	struct port *port;

	int ret;

	port_name = snobj_str_get(q);
	if (!port_name)
		return snobj_err(EINVAL, "Argument must be a name in str");
	
	port = find_port(port_name);
	if (!port)
		return snobj_err(ENOENT, "No port `%s' found", port_name);

	ret = destroy_port(port);
	if (ret)
		return snobj_errno(-ret);

	return NULL;
}

static struct snobj *handle_get_port_stats(struct snobj *q)
{
	const char *port_name;

	struct port *port;

	port_stats_t stats;

	struct snobj *r;
	struct snobj *inc;
	struct snobj *out;

	port_name = snobj_str_get(q);
	if (!port_name)
		return snobj_err(EINVAL, "Argument must be a name in str");
	
	port = find_port(port_name);
	if (!port)
		return snobj_err(ENOENT, "No port '%s' found", port_name);

	get_port_stats(port, &stats);

	inc = snobj_map();
	snobj_map_set(inc, "packets", snobj_uint(stats[PACKET_DIR_INC].packets));
	snobj_map_set(inc, "dropped", snobj_uint(stats[PACKET_DIR_INC].dropped));
	snobj_map_set(inc, "bytes",   snobj_uint(stats[PACKET_DIR_INC].bytes));

	out = snobj_map();
	snobj_map_set(out, "packets", snobj_uint(stats[PACKET_DIR_OUT].packets));
	snobj_map_set(out, "dropped", snobj_uint(stats[PACKET_DIR_OUT].dropped));
	snobj_map_set(out, "bytes",   snobj_uint(stats[PACKET_DIR_OUT].bytes));

	r = snobj_map();
	snobj_map_set(r, "inc", inc);
	snobj_map_set(r, "out", out);
	snobj_map_set(r, "timestamp", snobj_double(get_epoch_time()));

	return r;
}

static struct snobj *handle_list_mclasses(struct snobj *q)
{
	struct snobj *r = snobj_list();
	struct ns_iter iter;

	struct mclass *cls;

	ns_init_iterator(&iter, NS_TYPE_MCLASS);

	while ((cls = (struct mclass *)ns_next(&iter)) != NULL)
		snobj_list_add(r, snobj_str(cls->name));

	ns_release_iterator(&iter);

	return r;
}

static struct snobj *handle_get_mclass_info(struct snobj *q)
{
	const char *cls_name;
	const struct mclass *cls;

	struct snobj *r;
	struct snobj *cmds;

	cls_name = snobj_str_get(q);

	if (!cls_name)
		return snobj_err(EINVAL, "Argument must be a name in str");

	if ((cls = find_mclass(cls_name)) == NULL)
		return snobj_err(ENOENT, "No module class '%s' found", 
				cls_name);

	cmds = snobj_list();
	for (int i = 0; i < MAX_COMMANDS; i++) {
		if (!cls->commands[i].cmd)
			break;

		snobj_list_add(cmds, snobj_str(cls->commands[i].cmd));
	}

	r = snobj_map();
	snobj_map_set(r, "name", snobj_str(cls->name));
	snobj_map_set(r, "help", snobj_str(cls->help ? : ""));
	snobj_map_set(r, "commands", cmds);

	return r;
}

static struct snobj *handle_reset_modules(struct snobj *q)
{
	struct module *m;

	while (list_modules((const struct module **)&m, 1, 0))
		destroy_module(m);

	log_info("*** All modules have been destroyed ***\n");
	return NULL;
}

static struct snobj *handle_list_modules(struct snobj *q)
{
	struct snobj *r;

	int cnt = 1;
	int offset;

	r = snobj_list();

	for (offset = 0; cnt != 0; offset += cnt) {
		const int arr_size = 16;
		const struct module *modules[arr_size];

		int i;
		
		cnt = list_modules(modules, arr_size, offset);

		for (i = 0; i < cnt; i++) {
			const struct module *m = modules[i];
			const struct mclass *mclass = m->mclass;

			struct snobj *module = snobj_map();

			snobj_map_set(module, "name", 
					snobj_str(m->name));
			snobj_map_set(module, "mclass", 
					snobj_str(mclass->name));
			if (mclass->get_desc) {
				snobj_map_set(module, "desc", 
						mclass->get_desc(m));
			}

			snobj_list_add(r, module);
		}
	};

	return r;
}

static struct snobj *handle_create_module(struct snobj *q)
{
	const char *mclass_name;
	const struct mclass *mclass;
	struct module *module;

	struct snobj *r;

	mclass_name = snobj_eval_str(q, "mclass");
	if (!mclass_name)
		return snobj_err(EINVAL, "Missing 'mclass' field");

	mclass = find_mclass(mclass_name);
	if (!mclass)
		return snobj_err(ENOENT, "No mclass '%s' found", mclass_name);

	module = create_module(snobj_eval_str(q, "name"), mclass, 
			snobj_eval(q, "arg"), &r);
	if (!module)
		return r;

	r = snobj_map();
	snobj_map_set(r, "name", snobj_str(module->name));

	return r;
}

static struct snobj *handle_destroy_module(struct snobj *q)
{
	const char *m_name;
	struct module *m;

	m_name = snobj_str_get(q);

	if (!m_name)
		return snobj_err(EINVAL, "Argument must be a name in str");

	if ((m = find_module(m_name)) == NULL)
		return snobj_err(ENOENT, "No module '%s' found", m_name);

	destroy_module(m);

	return NULL;
}

static struct snobj *collect_igates(struct module *m)
{
	struct snobj *igates = snobj_list();

	for (int i = 0; i < m->igates.curr_size; i++) {
		if (!is_active_gate(&m->igates, i))
			continue;

		struct snobj *igate = snobj_map();
		struct gate *g = m->igates.arr[i];

		struct snobj *ogates = snobj_list();
		struct gate *og;

		snobj_map_set(igate, "igate", snobj_uint(i));
		
		cdlist_for_each_entry(og, &g->in.ogates_upstream, 
				out.igate_upstream) 
		{
			struct snobj *ogate = snobj_map();
			snobj_map_set(ogate, "ogate", snobj_uint(og->gate_idx));
			snobj_map_set(ogate, "name", snobj_str(og->m->name));
			snobj_list_add(ogates, ogate);
		}

		snobj_map_set(igate, "ogates", ogates);

		snobj_list_add(igates, igate);
	}

	return igates;
}

static struct snobj *collect_ogates(struct module *m)
{
	struct snobj *ogates = snobj_list();

	for (int i = 0; i < m->ogates.curr_size; i++) {
		if (!is_active_gate(&m->ogates, i))
			continue;

		struct snobj *ogate = snobj_map();
		struct gate *g = m->ogates.arr[i];

		snobj_map_set(ogate, "ogate", snobj_uint(i));
#if TRACK_GATES
		snobj_map_set(ogate, "cnt", snobj_uint(g->cnt));
		snobj_map_set(ogate, "pkts", snobj_uint(g->pkts));
		snobj_map_set(ogate, "timestamp", 
				snobj_double(get_epoch_time()));
#endif
		snobj_map_set(ogate, "name", 
				snobj_str(g->out.igate->m->name));
		snobj_map_set(ogate, "igate",
				snobj_uint(g->out.igate->gate_idx));
		
		snobj_list_add(ogates, ogate);
	}

	return ogates;
}

static struct snobj *collect_metadata(struct module *m)
{
	struct snobj *metadata = snobj_list();

	for (int i = 0; i < m->num_attrs; i++) {
		struct snobj *attr = snobj_map();

		snobj_map_set(attr, "name", snobj_str(m->attrs[i].name));
		snobj_map_set(attr, "size", snobj_uint(m->attrs[i].size));

		switch (m->attrs[i].mode) {
		case MT_READ:
			snobj_map_set(attr, "mode", snobj_str("read"));
			break;
		case MT_WRITE:
			snobj_map_set(attr, "mode", snobj_str("write"));
			break;
		case MT_UPDATE:
			snobj_map_set(attr, "mode", snobj_str("update"));
			break;
		default:
			assert(0);
		}

		snobj_map_set(attr, "offset", snobj_int(m->attr_offsets[i]));

		snobj_list_add(metadata, attr);
	}

	return metadata;
}

static struct snobj *handle_get_module_info(struct snobj *q)
{
	const char *m_name;
	struct module *m;

	struct snobj *r;

	m_name = snobj_str_get(q);

	if (!m_name)
		return snobj_err(EINVAL, "Argument must be a name in str");

	if ((m = find_module(m_name)) == NULL)
		return snobj_err(ENOENT, "No module '%s' found", m_name);

	r = snobj_map();

	snobj_map_set(r, "name", snobj_str(m->name));
	snobj_map_set(r, "mclass", snobj_str(m->mclass->name));

	if (m->mclass->get_desc)
		snobj_map_set(r, "desc", m->mclass->get_desc(m));

	if (m->mclass->get_dump)
		snobj_map_set(r, "dump", m->mclass->get_dump(m));

	snobj_map_set(r, "igates", collect_igates(m));
	snobj_map_set(r, "ogates", collect_ogates(m));
	snobj_map_set(r, "metadata", collect_metadata(m));

	return r;
}

static struct snobj *handle_connect_modules(struct snobj *q)
{
	const char *m1_name;
	const char *m2_name;
	gate_idx_t ogate;
	gate_idx_t igate;

	struct module *m1;
	struct module *m2;

	int ret;

	m1_name = snobj_eval_str(q, "m1");
	m2_name = snobj_eval_str(q, "m2");
	ogate = snobj_eval_uint(q, "ogate");
	igate = snobj_eval_uint(q, "igate");

	if (!m1_name || !m2_name)
		return snobj_err(EINVAL, "Missing 'm1' or 'm2' field");

	if ((m1 = find_module(m1_name)) == NULL)
		return snobj_err(ENOENT, "No module '%s' found", m1_name);

	if ((m2 = find_module(m2_name)) == NULL)
		return snobj_err(ENOENT, "No module '%s' found", m2_name);

	ret = connect_modules(m1, ogate, m2, igate);
	if (ret < 0)
		return snobj_err(-ret, "Connection %s:%d->%d:%s failed", 
			m1_name, ogate, igate, m2_name);

	return NULL;
}

static struct snobj *handle_disconnect_modules(struct snobj *q)
{
	const char *m_name;
	gate_idx_t ogate;

	struct module *m;

	int ret;

	m_name = snobj_eval_str(q, "name");
	ogate = snobj_eval_uint(q, "ogate");

	if (!m_name)
		return snobj_err(EINVAL, "Missing 'name' field");

	if ((m = find_module(m_name)) == NULL)
		return snobj_err(ENOENT, "No module '%s' found", m_name);

	ret = disconnect_modules(m, ogate);
	if (ret < 0)
		return snobj_err(-ret, "Disconnection %s:%d failed", 
			m_name, ogate);

	return NULL;
}

static struct snobj *handle_attach_task(struct snobj *q)
{
	const char *m_name;
	const char *tc_name;

	task_id_t tid;

	struct module *m;
	struct task *t;

	m_name = snobj_eval_str(q, "name");

	if (!m_name)
		return snobj_err(EINVAL, "Missing 'name' field");

	if ((m = find_module(m_name)) == NULL)
		return snobj_err(ENOENT, "No module '%s' found", m_name);

	tid = snobj_eval_uint(q, "taskid");
	if (tid >= MAX_TASKS_PER_MODULE)
		return snobj_err(EINVAL, "'taskid' must be between 0 and %d",
				MAX_TASKS_PER_MODULE - 1);

	if ((t = m->tasks[tid]) == NULL)
		return snobj_err(ENOENT, "Task %s:%hu does not exist", 
				m_name, tid);

	tc_name = snobj_eval_str(q, "tc");

	if (tc_name) {
		struct tc *c;

		c = ns_lookup(NS_TYPE_TC, tc_name);
		if (!c)
			return snobj_err(ENOENT, "No TC '%s' found", tc_name);

		task_attach(t, c);
	} else {
		int wid;		/* TODO: worker_id_t */

		if (task_is_attached(t))
			return snobj_err(EBUSY, "Task %s:%hu is already "
					"attached to a TC", m_name, tid);

		wid = snobj_eval_uint(q, "wid");
		if (wid >= MAX_WORKERS)
			return snobj_err(EINVAL, "'wid' must be between 0 and %d",
					MAX_WORKERS - 1);

		if (!is_worker_active(wid))
			return snobj_err(EINVAL, "Worker %d does not exist", wid);

		assign_default_tc(wid, t);
	}

	return NULL;
}

static struct snobj *handle_enable_tcpdump(struct snobj *q)
{
	const char *m_name;
	const char *fifo;
	gate_idx_t ogate;

	struct module *m;

	int ret;

	m_name = snobj_eval_str(q, "name");
	ogate = snobj_eval_uint(q, "ogate");
	fifo = snobj_eval_str(q, "fifo");

	if (!m_name)
		return snobj_err(EINVAL, "Missing 'name' field");

	if ((m = find_module(m_name)) == NULL)
		return snobj_err(ENOENT, "No module '%s' found", m_name);

	if (ogate >= m->ogates.curr_size)
		return snobj_err(EINVAL, "Output gate '%hu' does not exist", 
				ogate);

	ret = enable_tcpdump(fifo, m, ogate);

	if (ret < 0) {
		return snobj_err(-ret, "Enabling tcpdump %s:%d failed",
				m_name, ogate);
	}

	return NULL;
}

static struct snobj *handle_disable_tcpdump(struct snobj *q)
{
	const char *m_name;
	gate_idx_t ogate;

	struct module *m;

	int ret;

	m_name = snobj_eval_str(q, "name");
	ogate = snobj_eval_uint(q, "ogate");

	if (!m_name)
		return snobj_err(EINVAL, "Missing 'name' field");

	if ((m = find_module(m_name)) == NULL)
		return snobj_err(ENOENT, "No module '%s' found", m_name);

	if (ogate >= m->ogates.curr_size)
		return snobj_err(EINVAL, "Output gate '%hu' does not exist", 
				ogate);

	ret = disable_tcpdump(m, ogate);

	if (ret < 0) {
		return snobj_err(-ret, "Disabling tcpdump %s:%d failed",
				m_name, ogate);
	}
	return NULL;
}

/* Adding this mostly to provide a reasonable way to exit when daemonized */
static struct snobj *handle_kill_bess(struct snobj *q)
{
	log_notice("Halt requested by a client\n");
	exit(EXIT_SUCCESS);

	/* Never called */
	return NULL;
}

static struct snobj *handle_not_implemented(struct snobj *q)
{
	return snobj_err(ENOTSUP, "Not implemented yet");
}

static struct handler_map sn_handlers[] = {
	/* remove all ports/modules/TCs/workers */
	{ "reset_all",		1, handle_reset_all },

	/* pause and resume all workers */
	{ "pause_all", 		0, handle_pause_all },
	{ "resume_all", 	0, handle_resume_all },

	{ "reset_workers",	1, handle_reset_workers },
	{ "list_workers",	0, handle_list_workers },
	{ "add_worker",		0, handle_add_worker },
	{ "delete_worker",	1, handle_not_implemented },

	{ "reset_tcs",		1, handle_reset_tcs },
	{ "list_tcs",		0, handle_list_tcs },
	{ "add_tc",		1, handle_add_tc },
	{ "get_tc_stats",	0, handle_get_tc_stats },

	{ "list_drivers",	0, handle_list_drivers },
	{ "import_driver",	0, handle_not_implemented },	/* TODO */

	{ "reset_ports",	1, handle_reset_ports },
	{ "list_ports",		0, handle_list_ports },
	{ "create_port", 	0, handle_create_port },
	{ "destroy_port",	0, handle_destroy_port },
	{ "get_port_stats",	0, handle_get_port_stats },

	{ "list_mclasses", 	0, handle_list_mclasses },
	{ "get_mclass_info",	0, handle_get_mclass_info },
	{ "import_mclass",	0, handle_not_implemented },	/* TODO */

	{ "reset_modules",	1, handle_reset_modules },
	{ "list_modules",	0, handle_list_modules },
	{ "create_module", 	1, handle_create_module },
	{ "destroy_module", 	1, handle_destroy_module },
	{ "get_module_info",	0, handle_get_module_info },
	{ "connect_modules", 	1, handle_connect_modules },
	{ "disconnect_modules",	1, handle_disconnect_modules },

	{ "attach_task",	1, handle_attach_task },

	{ "enable_tcpdump",	1, handle_enable_tcpdump },
	{ "disable_tcpdump",	1, handle_disable_tcpdump },

	{ "kill_bess",		1, handle_kill_bess },

	{ NULL, 		0, NULL }
};

static struct snobj *handle_snobj_bess(struct snobj *q)
{
	struct snobj *arg;
	const char *s;

	s = snobj_eval_str(q, "cmd");
	if (!s)
		return snobj_err(EINVAL, "Missing 'cmd' field");

	arg = snobj_map_get(q, "arg");

	for (int i = 0; sn_handlers[i].cmd != NULL; i++) {
		if (strcmp(s, sn_handlers[i].cmd) != 0)
			continue;

		if (sn_handlers[i].pause_needed && is_any_worker_running())
			return snobj_err(EBUSY, "There is a running worker");

		return sn_handlers[i].func(arg);
	}

	return snobj_err(ENOTSUP, "Unknown command in 'cmd': '%s'", s);
}

struct snobj *
run_module_command(struct module *m, const char *cmd, struct snobj *arg)
{
	const struct mclass *cls = m->mclass;

	for (int i = 0; i < MAX_COMMANDS; i++) {
		if (!cls->commands[i].cmd)
			break;

		if (strcmp(cls->commands[i].cmd, cmd) == 0) {
			if (!cls->commands[i].mt_safe && 
					is_any_worker_running())
			{
				return snobj_err(EBUSY, 
						"There is a running worker and "
						"command '%s' is not MT safe",
						cmd);
			}

			return cls->commands[i].func(m, cmd, arg);
		}
	}

	return snobj_err(ENOTSUP, "'%s' does not support command '%s'",
			cls->name, cmd);
}

static struct snobj *handle_snobj_module(struct snobj *q)
{
	const char *m_name;
	const char *cmd;

	struct module *m;

	struct snobj *arg;

	m_name = snobj_eval_str(q, "name");
	if (!m_name)
		return snobj_err(EINVAL, "Missing module name field 'name'");

	if ((m = find_module(m_name)) == NULL)
		return snobj_err(ENOENT, "No module '%s' found", m_name);

	cmd = snobj_eval_str(q, "cmd");
	if (!cmd)
		return snobj_err(EINVAL, "Missing command name field 'cmd'");

	arg = snobj_eval(q, "arg");
	if (!arg) {
		struct snobj *ret;

		arg = snobj_nil();
		ret = run_module_command(m, cmd, arg);
		snobj_free(arg);
		return ret;
	} else
		return run_module_command(m, cmd, arg);
}

struct snobj *handle_request(struct client *c, struct snobj *q)
{
	struct snobj *r = NULL;
	const char *s;

	if (global_opts.debug_mode) {
		log_debug("Request:\n");
		snobj_dump(q);
	}

	if (q->type != TYPE_MAP) {
		r = snobj_err(EINVAL, "The message must be a map");
		goto reply;
	}

	s = snobj_eval_str(q, "to");
	if (!s) {
		r = snobj_str("There is no 'to' field");
		goto reply;
	}

	if (strcmp(s, "bess") == 0) {
		r = handle_snobj_bess(q);
	} else if (strcmp(s, "module") == 0) {
		r = handle_snobj_module(q);
	} else
		r = snobj_err(EINVAL, "Unknown destination in 'to': %s", s);

reply:
	/* No response was made? (normally means "success") */
	if (!r)
		r = snobj_nil();

	if (global_opts.debug_mode) {
		log_debug("Response:\n");
		snobj_dump(r);
	}

	return r;
}
