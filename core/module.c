#include <assert.h>

#include <rte_cycles.h>
#include <rte_malloc.h>

#include "module.h"
#include "dpdk.h"
#include "time.h"
#include "tc.h"
#include "namespace.h"

static scope_component scope_components[100];
static int curr_scope_id = 0;

task_id_t register_task(struct module *m, void *arg)
{
	task_id_t id;
	struct task *t;

	/* Module class must define run_task() to register a task */
	if (!m->mclass->run_task)
		return INVALID_TASK_ID;

	for (id = 0; id < MAX_TASKS_PER_MODULE; id++)
		if (m->tasks[id] == NULL)
			goto found;

	/* cannot find an empty slot */
	return INVALID_TASK_ID;

found:
	t = task_create(m, arg);
	if (!t)
		return INVALID_TASK_ID;

	m->tasks[id] = t;

	return id;
}

task_id_t task_to_tid(struct task *t)
{
	struct module *m = t->m;

	for (task_id_t id = 0; id < MAX_TASKS_PER_MODULE; id++)
		if (m->tasks[id] == t)
			return id;

	return INVALID_TASK_ID;
}

int num_module_tasks(struct module *m)
{
	int cnt = 0;

	for (task_id_t id = 0; id < MAX_TASKS_PER_MODULE; id++)
		if (m->tasks[id])
			cnt++;

	return cnt;
}

size_t list_modules(const struct module **p_arr, size_t arr_size, size_t offset)
{
	int ret = 0;
	int iter_cnt = 0;

	struct ns_iter iter;

	ns_init_iterator(&iter, NS_TYPE_MODULE);
	while (1) {
		struct module *module = (struct module *) ns_next(&iter);
		if (!module)
			break;

		if (iter_cnt++ < offset)
			continue;

		if (ret >= arr_size)
			break;

		p_arr[ret++] = module;
	}
	ns_release_iterator(&iter);

	return ret;
}

static void set_default_name(struct module *m)
{
	char *fmt;

	int i;

	if (m->mclass->def_module_name) {
		fmt = alloca(strlen(m->mclass->def_module_name) + 16);
		strcpy(fmt, m->mclass->def_module_name);
	} else {
		const char *template = m->mclass->name;
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

		ret = snprintf(m->name, MODULE_NAME_LEN, fmt, i);
		assert(ret < MODULE_NAME_LEN);

		if (!find_module(m->name))
			break;	/* found an unallocated name! */
	}
}

static int register_module(struct module *m)
{
	int ret;

	ret = ns_insert(NS_TYPE_MODULE, m->name, (void *) m);
	if (ret < 0)
		return ret;

	return 0;
}

void deadend(struct module *m, struct pkt_batch *batch)
{
	ctx.silent_drops += batch->cnt;
	snb_free_bulk(batch->pkts, batch->cnt);
}


static void destroy_all_tasks(struct module *m)
{
	for (task_id_t i = 0; i < MAX_TASKS_PER_MODULE; i++) {
		if (m->tasks[i]) {
			task_destroy(m->tasks[i]);
			m->tasks[i] = NULL;	/* just in case */
		}
	}
}

/* returns a pointer to the created module.
 * if error, returns NULL and *perr is set */
struct module *create_module(const char *name,
		const struct mclass *mclass,
		struct snobj *arg,
		struct snobj **perr)
{
	struct module *m = NULL;
	int ret = 0;
	*perr = NULL;

	if (name && find_module(name)) {
		*perr = snobj_err(EEXIST, "Module '%s' already exists", name);
		goto fail;
	}

	m = rte_zmalloc("module", sizeof(struct module) + mclass->priv_size, 0);
	if (!m) {
		*perr = snobj_errno(ENOMEM);
		goto fail;
	}

	m->mclass = mclass;
	m->name = rte_zmalloc("name", MODULE_NAME_LEN, 0);

	if (!m->name) {
		*perr = snobj_errno(ENOMEM);
		goto fail;
	}

	if (!name)
		set_default_name(m);
	else
		snprintf(m->name, MODULE_NAME_LEN, "%s", name);

#if 0
	if (ops->timer) {
		for (i = 0; i < num_workers; i++) {
			m->timers[i] = rte_zmalloc_socket(NULL,
					sizeof(struct rte_timer), 0,
					wid_to_sid_map[i]);
			assert(m->timers[i]);

			rte_timer_init(m->timers[i]);
		}
	}
#endif

	if (mclass->init) {
		*perr = mclass->init(m, arg);
		if (*perr != NULL)
			goto fail;
	}

	ret = register_module(m);
	if (ret != 0) {
		*perr = snobj_errno(-ret);
		goto fail;
	}

	return m;

fail:
	if (m) {
		destroy_all_tasks(m);
		rte_free(m->name);
	}

	rte_free(m);

	return NULL;
}

void destroy_module(struct module *m)
{
	if (m->mclass->deinit)
		m->mclass->deinit(m);

	/* disconnect from upstream modules. */
	for (int i = 0; i < m->igates.curr_size; i++) {
		if (!is_active_gate(&m->igates, i))
			continue;

		struct gate *igate = m->igates.arr[i];
		struct gate *ogate;
		struct gate *ogate_next;

		cdlist_for_each_entry_safe(ogate, ogate_next,
				&igate->in.ogates_upstream, out.igate_upstream)
		{
			disconnect_modules(ogate->m, ogate->gate_idx);
		}
	}

	/* disconnect downstream modules */
	for (gate_idx_t i = 0; i < m->ogates.curr_size; i++)
		disconnect_modules(m, i);

	destroy_all_tasks(m);

	ns_remove(m->name);

	rte_free(m->name);
	rte_free(m->ogates.arr);
	rte_free(m->igates.arr);
	rte_free(m);
}


static int grow_gates(struct module *m, struct gates *gates, gate_idx_t gate)
{
	struct gate **new_arr;
	gate_idx_t old_size;
	gate_idx_t new_size;

	new_size = gates->curr_size ? : 1;

	while (new_size <= gate)
		new_size *= 2;

	if (new_size > MAX_GATES)
		new_size = MAX_GATES;

	new_arr = rte_realloc(gates->arr,
			sizeof(struct gate *) * new_size, 0);
	if (!new_arr)
		return -ENOMEM;

	gates->arr = new_arr;

	old_size = gates->curr_size;
	gates->curr_size = new_size;

	/* initialize the newly created gates */
	memset(&gates->arr[old_size], 0,
			sizeof(struct gate *) * (new_size - old_size));

	return 0;
}

/*
 * BEGIN METADATA OFFSET COMPUTATION
 */

/* Adds module to the current scope component. */
static void add_module_to_component(struct module *m, metadata_field *field)
{
	scope_component *component = &scope_components[curr_scope_id];

	/* module has already been added to current scope component */
	for (int i = 0; i < component->num_modules; i++) {
		if (component->modules[i] == m)
			return;
	}

	if (component->num_modules == 0) {
		component->num_modules++;
		component->len = field->len;
		component->name = field->name;
		component->modules = rte_malloc("module array",
						sizeof(struct module *), 0);
		component->modes = rte_malloc("mode array",
				       	      sizeof(metadata_mode), 0);
		component->modules[0] = m;
		component->modes[0] = field->mode;
	} else {
		component->num_modules++;
		component->modules = rte_realloc(component->modules,
						 sizeof(struct module *) * component->num_modules,
						 0);
		component->modes = rte_realloc(component->modes,
					       sizeof(metadata_mode) * component->num_modules,
					       0);
		component->modules[component->num_modules - 1] = m;		
		component->modes[component->num_modules - 1] = field->mode;		
	}
}

/* Traverses graph upstream to help identify a scope component. */
static void traverse_upstream(struct module *m, metadata_field *field)
{
	metadata_field *read_field = NULL;
	metadata_field *written_field = NULL;

	for (int i = 0; i < m->num_fields; i++) {
		metadata_field *curr_field = &m->fields[i];

		if (strcmp(curr_field->name, field->name) == 0 &&
		    curr_field->len == field->len) {

			if (curr_field->mode == WRITE) {
				written_field = curr_field;
			} else {
				read_field = curr_field;
			}
		}
	}

	/* handles the case where a module writes and reads the same field */
	if (read_field && read_field->scope_id == curr_scope_id)
		goto up;

	/* found a module that writes the field; end of scope component */
	if (written_field && written_field->scope_id == -1) {
		identify_scope_component(m, written_field);
		return;
	} else if (written_field) {
		return;
	}

up:
	for (int i = 0; i < m->igates.curr_size; i++) {
		struct gate *g = m->igates.arr[i];
		struct gate *og;

		cdlist_for_each_entry(og, &g->in.ogates_upstream,
				out.igate_upstream)
		{
			struct module *module = og->m;
			traverse_upstream(module, field);
		}
	}
}

/*
 * Traverses graph downstream to help identify a scope component.
 * Return value of -1 indicates that module is not part of the scope component.
 * Return value of 0 indicates that module is part of the scope component.
 */
static int traverse_downstream(struct module *m, metadata_field *field)
{
	struct gate *gate;
	metadata_field *written_field = NULL;
	metadata_field *read_field = NULL;
	int in_scope = 0;

	for (int i = 0; i < m->num_fields; i++) {
		metadata_field *curr_field = &m->fields[i];

		if (strcmp(curr_field->name, field->name) == 0 &&
		    curr_field->len == field->len) {

			if (curr_field->mode == WRITE) {
				written_field = curr_field;
			} else {
				read_field = curr_field;
			}
		}
	}

	if (read_field) {
		add_module_to_component(m, read_field);
		read_field->scope_id = curr_scope_id;

		if (!written_field) {
			for (int i = 0; i < m->ogates.curr_size; i++) {
				gate = m->ogates.arr[i];
				struct module *m_next = (struct module *)gate->arg;
				traverse_downstream(m_next, field);
			}
		}

		traverse_upstream(m, field);
		return 0;
	} else {
		if (written_field)
			return -1;

		for (int i = 0; i < m->ogates.curr_size; i++) {
			gate = m->ogates.arr[i];
			struct module *m_next = (struct module *)gate->arg;
			if (traverse_downstream(m_next, field) != -1)
				in_scope = 1;
		}

		if (in_scope) {
			add_module_to_component(m, field);
			traverse_upstream(m, field);
			return 0;
		}

		return -1;
	}
}


/*
 * Given a module that writes a field, identifies the portion of corresponding scope 
 * component that lies downstream from this module. 
 */
void identify_scope_component(struct module *m, metadata_field *field)
{
	struct gate *gate;

	add_module_to_component(m, field);
	field->scope_id = curr_scope_id;

	for (int i = 0; i < m->ogates.curr_size; i++) {
		gate = m->ogates.arr[i];
		struct module *m_next = (struct module *)gate->arg;
		traverse_downstream(m_next, field);
	}
}

/* static void reset_cycle_detection()
{
	struct ns_iter iter;
	ns_init_iterator(&iter, NS_TYPE_MODULE);
	while (1) {
		struct module *m = (struct module *) ns_next(&iter);
		if (!m)
			break;
		
		m->upstream_cycle = -1;
		m->downstream_cycle = -1;
	}
	ns_release_iterator(&iter);
} */

static void prepare_metadata_computation()
{
	struct ns_iter iter;
	ns_init_iterator(&iter, NS_TYPE_MODULE);
	while (1) {
		struct module *m = (struct module *) ns_next(&iter);
		if (!m)
			break;
		
		for (int i = 0; i < m->num_fields; i++)
			m->fields[i].scope_id = -1;
	}
	ns_release_iterator(&iter);
}

static void cleanup_metadata_computation()
{
	for (int i = 0; i < curr_scope_id; i++) {
		scope_component component = scope_components[i];
		if (component.len == 0)
			continue;
		rte_free(component.modules);
		rte_free(component.modes);
	}

	memset(&scope_components, 0, 100 * sizeof(scope_component));
	curr_scope_id = 0;
}


static int scope_overlaps(int i1, int i2) {
	scope_component n1 = scope_components[i1];
	scope_component n2 = scope_components[i2];

	for (int i = 0; i < n1.num_modules; i++) {
		for (int j = 0; j < n2.num_modules; j++) {
			if (n1.modules[i] == n2.modules[j])
				return 1;
		}
	}
	return 0;
}

/*
 * Given a scope component id, identifies the set of all overlapping components
 * including this scope component.
 */
static void find_overlapping_components(int index, int *offset)
{
	scope_components[index].offset = *offset;
	*offset = *offset + scope_components[index].len;
	scope_components[index].visited = 1;

	for (int j = index; j < curr_scope_id; j++) {
		if (scope_components[j].visited)
			continue;

		if (scope_overlaps(index,j))
			find_overlapping_components(j, offset);
	}
}

static void fill_offset_arrays()
{
	for (int i = 0; i < curr_scope_id; i++) {
		struct module **modules = scope_components[i].modules;
		char *name = scope_components[i].name;
		int len = scope_components[i].len;
		int offset = scope_components[i].offset;

		for (int j = 0; j < scope_components[i].num_modules; j++) {
			struct module *m = modules[j];

			for (int k = 0; k < m->num_fields; k++) {
				if (strcmp(m->fields[k].name, name) == 0 &&
				    m->fields[k].len == len &&
				    scope_components[i].modes[j] == m->fields[k].mode) {

					m->field_offsets[k]= offset;
					if (m->fields[k].mode == READ)
						log_info("Module %s using offset %d to read field %s\n",
							  m->name, offset, name);
					else
						log_info("Module %s using offset %d to write field %s\n",
							  m->name, offset, name);

				}
			}
		}

	}
}

/* Calculates offsets after scope components are identified */
static void assign_offsets()
{
	int offset = 0;
	for (int i = 0; i < curr_scope_id; i++) {
		if (scope_components[i].visited)
			continue;

		offset = 0;
		find_overlapping_components(i, &offset);
	}

	fill_offset_arrays();
}

/* Main entry point for calculating metadata offsets. */
void compute_metadata_offsets()
{
	prepare_metadata_computation();

	struct ns_iter iter;
	ns_init_iterator(&iter, NS_TYPE_MODULE);
	while (1) {
		struct module *m = (struct module *) ns_next(&iter);
		if (!m)
			break;

		for (int i = 0; i < m->num_fields; i++) {
			metadata_field *field = &m->fields[i];
			if (field->mode == WRITE && field->scope_id == -1) {
				identify_scope_component(m, field);
				curr_scope_id++;
			}
		}
	}
	ns_release_iterator(&iter);

	for (int i = 0; i < curr_scope_id; i++) {
		log_info("SCOPE COMPONENT FOR FIELD %s\n", scope_components[i].name);
		log_info("{ ");
		for (int j = 0; j < scope_components[i].num_modules; j++) {
			log_info("%s ", scope_components[i].modules[j]->name);
		}
		log_info("}\n");
	}

	assign_offsets();
	cleanup_metadata_computation();
}

/* Checks if field is supported upstream. */
static int field_supported(struct module *m, metadata_field *field)
{
	for (int i = 0; i < m->num_fields; i++) {
		metadata_field curr_field = m->fields[i];
		if (curr_field.mode == WRITE &&
		    strcmp(curr_field.name, field->name) == 0  &&
		    curr_field.len == field->len)
			return 1;
	}

	if (m->igates.curr_size == 0)
		return 0;

	for (int i = 0; i < m->igates.curr_size; i++) {
		struct gate *g = m->igates.arr[i];
		struct gate *og;

		cdlist_for_each_entry(og, &g->in.ogates_upstream,
				out.igate_upstream)
		{
			if (!field_supported((struct module *)og->m, field))
				return 0;
		}
	}

	return 1;
}

/* Checks if configuration is valid with respect to metadata */
int valid_metadata_configuration()
{
	struct ns_iter iter;

	ns_init_iterator(&iter, NS_TYPE_MODULE);
	while (1) {
		struct module *m = (struct module *) ns_next(&iter);

		if (!m)
			break;

		for (int i = 0; i < m->num_fields; i++) {
			metadata_field field = m->fields[i];
			if (field.mode == READ && !field_supported(m, &field))
				return 0;
		}
	}
	ns_release_iterator(&iter);

	return 1;
}


/*
 * END METADATA OFFSET COMPUTATION
 */


/* returns -errno if fails */
int connect_modules(struct module *m_prev, gate_idx_t ogate_idx,
		    struct module *m_next, gate_idx_t igate_idx)
{
	struct gate *ogate;
	struct gate *igate;

	if (!m_next->mclass->process_batch)
		return -EINVAL;

	if (ogate_idx >= m_prev->mclass->num_ogates || ogate_idx >= MAX_GATES)
		return -EINVAL;

	if (igate_idx >= m_next->mclass->num_igates || igate_idx >= MAX_GATES)
		return -EINVAL;

	if (ogate_idx >= m_prev->ogates.curr_size) {
		int ret = grow_gates(m_prev, &m_prev->ogates, ogate_idx);
		if (ret)
			return ret;
	}

	/* already being used? */
	if (is_active_gate(&m_prev->ogates, ogate_idx))
		return -EBUSY;

	if (igate_idx >= m_next->igates.curr_size) {
		int ret = grow_gates(m_next, &m_next->igates, igate_idx);
		if (ret)
			return ret;
	}

	ogate = rte_zmalloc("gate", sizeof(struct gate), 0);
	if (!ogate)
		return -ENOMEM;

	m_prev->ogates.arr[ogate_idx] = ogate;

	igate = m_next->igates.arr[igate_idx];
	if (!igate) {
		igate = rte_zmalloc("gate", sizeof(struct gate), 0);
		if (!igate) {
			rte_free(ogate);
			return -ENOMEM;
		}

		m_next->igates.arr[igate_idx] = igate;

		igate->m = m_next;
		igate->gate_idx = igate_idx;
		igate->f = m_next->mclass->process_batch;
		igate->arg = m_next;
		cdlist_head_init(&igate->in.ogates_upstream);
	}

	ogate->m = m_prev;
	ogate->gate_idx = ogate_idx;
	ogate->f = m_next->mclass->process_batch;
	ogate->arg = m_next;
	ogate->out.igate = igate;
	ogate->out.igate_idx = igate_idx;

	cdlist_add_tail(&igate->in.ogates_upstream, &ogate->out.igate_upstream);

	return 0;
}

int disconnect_modules(struct module *m_prev, gate_idx_t ogate_idx)
{
	struct gate *ogate;
	struct gate *igate;

	if (ogate_idx >= m_prev->mclass->num_ogates)
		return -EINVAL;

	/* no error even if the ogate is unconnected already */
	if (!is_active_gate(&m_prev->ogates, ogate_idx))
		return 0;

	ogate = m_prev->ogates.arr[ogate_idx];
	if (!ogate)
		return 0;

	igate = ogate->out.igate;

	/* Does the igate become inactive as well? */
	cdlist_del(&ogate->out.igate_upstream);
	if (cdlist_is_empty(&igate->in.ogates_upstream)) {
		struct module *m_next = igate->m;
		gate_idx_t igate_idx = ogate->out.igate_idx;
		m_next->igates.arr[igate_idx] = NULL;
		rte_free(igate);
	}

	rte_free(ogate);
	m_prev->ogates.arr[ogate_idx] = NULL;

	return 0;
}

#if 0
void init_module_worker()
{
	int i;

	for (i = 0; i < num_modules; i++) {
		struct module *mod = modules[i];

		if (mod->mclass->init_worker)
			mod->mclass->init_worker(mod);
	}
}
#endif

#if SN_TRACE_MODULES
#define MAX_TRACE_DEPTH		32
#define MAX_TRACE_BUFSIZE	4096

struct callstack {
	int depth;

	int newlined;
	int indent[MAX_TRACE_DEPTH];
	int curr_indent;

	int buflen;
	char buf[MAX_TRACE_BUFSIZE];
};

__thread struct callstack worker_callstack;

void _trace_start(struct module *mod, char *type)
{
	struct callstack *s = &worker_callstack;

	assert(s->depth == 0);
	assert(s->buflen == 0);

	s->buflen = snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen,
			"Worker %d %-8s | %s", current_wid, type, mod->name);

	s->curr_indent = s->buflen;
}

void _trace_end(int print_out)
{
	struct callstack *s = &worker_callstack;

	assert(s->depth == 0);
	s->buflen = 0;
	s->newlined = 0;

	if (print_out)
		log_debug("%s", s->buf);
}

void _trace_before_call(struct module *mod, struct module *next,
			struct pkt_batch *batch)
{
	struct callstack *s = &worker_callstack;
	int len;

	s->indent[s->depth] = s->curr_indent;

	if (s->newlined) {
		s->buflen += snprintf(s->buf + s->buflen,
				MAX_TRACE_BUFSIZE - s->buflen,
				"%*s", s->curr_indent, "");
	}

	len = snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen,
			" ---(%d)--> %s", batch->cnt, next->name);

	s->buflen += len;
	s->curr_indent += len;

	s->depth++;
	assert(s->depth < MAX_TRACE_DEPTH);

	s->newlined = 0;
}

void _trace_after_call(void)
{
	struct callstack *s = &worker_callstack;

	s->depth--;

	if (!s->newlined) {
		s->newlined = 1;

		s->buflen += snprintf(s->buf + s->buflen,
				MAX_TRACE_BUFSIZE - s->buflen, "\n");
	}

	s->curr_indent = s->indent[s->depth];
}
#endif

struct module *find_module(const char *name)
{
	return (struct module *)ns_lookup(NS_TYPE_MODULE, name);
}

#if TCPDUMP_GATES
int enable_tcpdump(const char* fifo, struct module *m, gate_idx_t ogate)
{
	static const struct pcap_hdr PCAP_FILE_HDR = {
		.magic_number = PCAP_MAGIC_NUMBER,
		.version_major = PCAP_VERSION_MAJOR,
		.version_minor = PCAP_VERSION_MINOR,
		.thiszone = PCAP_THISZONE,
		.sigfigs = PCAP_SIGFIGS,
		.snaplen = PCAP_SNAPLEN,
		.network = PCAP_NETWORK,
	};

	int fd;
	int ret;

	/* Don't allow tcpdump to be attached to gates that are not active */
	if (!is_active_gate(&m->ogates, ogate))
		return -EINVAL;

	fd = open(fifo, O_WRONLY | O_NONBLOCK);
	if (fd < 0)
		return -errno;

	/* Looooong time ago Linux ignored O_NONBLOCK in open().
	 * Try again just in case. */
	ret = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	if (ret < 0) {
		close(fd);
		return -errno;
	}

	ret = write(fd, &PCAP_FILE_HDR, sizeof(PCAP_FILE_HDR));
	if (ret < 0) {
		close(fd);
		return -errno;
	}

	m->ogates.arr[ogate]->fifo_fd = fd;
	m->ogates.arr[ogate]->tcpdump = 1;

	return 0;
}

int disable_tcpdump(struct module *m, gate_idx_t ogate)
{
	if (!is_active_gate(&m->ogates, ogate))
		return -EINVAL;

	if (!m->ogates.arr[ogate]->tcpdump)
		return -EINVAL;

	m->ogates.arr[ogate]->tcpdump = 0;
	close(m->ogates.arr[ogate]->fifo_fd);

	return 0;
}

void dump_pcap_pkts(struct gate *gate, struct pkt_batch *batch)
{
	struct timeval tv;

	int ret = 0;
	int fd = gate->fifo_fd;
	int packets = 0;

	gettimeofday(&tv, NULL);

	for (int i = 0; i < batch->cnt; i++) {
		struct snbuf* pkt = batch->pkts[i];
		int len = pkt->mbuf.data_len;
		struct pcap_rec_hdr *pkthdr =
			(struct pcap_rec_hdr*) snb_prepend(pkt,
				sizeof(struct pcap_rec_hdr));

		pkthdr->ts_sec = tv.tv_sec;
		pkthdr->ts_usec = tv.tv_usec;
		pkthdr->orig_len = pkthdr->incl_len = len;
		assert(len < PCAP_SNAPLEN);
		ret = write(fd, snb_head_data(pkt), pkt->mbuf.data_len);
		assert(pkt->mbuf.data_len < PIPE_BUF);

		if (ret < 0) {
			if (errno == EPIPE) {
				log_debug("Stopping dump\n");
				gate->tcpdump = 0;
				gate->fifo_fd = 0;
				close(fd);
			}
			return;
		} else {
			assert(ret == pkt->mbuf.data_len);
			packets++;
		}

		snb_adj(pkt, sizeof(struct pcap_rec_hdr));
	}
}
#endif

#if 0
static __thread struct module *timer_fired_mods[MAX_MODULES];
static __thread int num_timer_fired_mods;

static void dpdk_timer_cb(struct rte_timer *timer, void *arg)
{
	struct module *mod = arg;

	timer_fired_mods[num_timer_fired_mods++] = mod;
}

/* returns number of packets processed */
static int run_timer(void)
{
	int ret = 0;
	int i;

	num_timer_fired_mods = 0;
	rte_timer_manage();

	for (i = 0; i < num_timer_fired_mods; i++) {
		struct module *mod = timer_fired_mods[i];

#if SN_TRACE_MODULES
	_trace_start(mod, "TIMER");
#endif

		ret += mod->ops->timer(mod);

#if SN_TRACE_MODULES
	_trace_end(ret != 0);
#endif
	}

	return ret;
}

void run_old_scheduler(void)
{
#if SN_CPU_USAGE
	uint64_t last_print = rdtsc();
	uint64_t last_tsc = rdtsc();
	uint64_t idle = 0;
	uint64_t cnt_idle = 0;
	uint64_t cnt_busy = 0;
	uint64_t packets = 0;
#endif

	int i;
	int ret;

	// module count before pause
	int pause_prev_mcount = modules_cnt;

	current_tsc = rdtsc();
	current_us = current_tsc * 1000000 / tsc_hz;

again:
	// record previous module count so we know where
	// to start initializing from
	if (request_pause) {
		pause_prev_mcount = modules_cnt;
		wid_to_paused_map[current_wid] = 1;
	}

	while (wid_to_paused_map[current_wid]) {
		continue;
	}

	rte_rmb();

	// new modules added - initialize
	if (pause_prev_mcount != modules_cnt) {
		for (i = pause_prev_mcount; i < modules_cnt; i++) {
			struct module *mod = modules[i];
			if (mod->ops->init_worker)
				mod->ops->init_worker(mod);
		}
		pause_prev_mcount = modules_cnt;
	}

	/* timer has a higher priority then polls */
	ret = run_timer();

	if (likely(ret == 0)) {
		for (i = 0; i < poll_list_cnt; i++)
			ret += do_poll(poll_list[i]);
	}

#if SN_CPU_USAGE
	current_tsc = rdtsc();
	current_us = current_tsc * 1000000 / tsc_hz;

	packets += ret;

	if (ret == 0) {
		idle += (current_tsc - last_tsc);
		cnt_idle++;
	} else
		cnt_busy++;

	if (unlikely(current_tsc - last_print > tsc_hz)) {
		if (unlikely(idle > tsc_hz))
			idle = tsc_hz;
		log_debug("Worker %d: %5.1f%%, "
			"loop %4.2fM/%4.2fM, "
			"idle %.2fus, "
			"busy %.2fus (%.2fus/pkt), "
			"%lu pkts\n",
				current_wid,
				(double)(tsc_hz - idle) / tsc_hz * 100.0,
				cnt_idle / 1000000.0, cnt_busy / 1000000.0,
				tsc_to_us(idle / (cnt_idle + 1)),
				tsc_to_us((tsc_hz - idle) / (cnt_busy + 1)),
				tsc_to_us((tsc_hz - idle) / (packets + 1)),
				packets);

		last_print = current_tsc;
		idle = 0;
		cnt_idle = 0;
		cnt_busy = 0;
		packets = 0;
	}

	last_tsc = current_tsc;
#endif
	goto again;
}

#endif
