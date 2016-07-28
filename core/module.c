#include "mem_alloc.h"
#include "dpdk.h"
#include "time.h"
#include "tc.h"
#include "namespace.h"
#include "module.h"

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

static void inherit_attr_list(const struct mclass *mclass, struct module *m)
{
	for (int i = 0; i < MAX_ATTRS_PER_MODULE; i++) {
		const struct mt_attr *attr = &mclass->attrs[i];
		int ret;

		/* end of the list is marked as a empty-string name */
		if (strlen(attr->name) == 0)
			return;

		ret = add_metadata_attr(m, attr->name, attr->size, attr->mode);
		assert(ret == i);
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

	m = mem_alloc(sizeof(struct module) + mclass->priv_size +
			/*hotfix*/ 128);
	if (!m) {
		*perr = snobj_errno(ENOMEM);
		goto fail;
	}

	m->mclass = mclass;
	m->name = mem_alloc(MODULE_NAME_LEN);

	if (!m->name) {
		*perr = snobj_errno(ENOMEM);
		goto fail;
	}

	if (!name)
		set_default_name(m);
	else
		snprintf(m->name, MODULE_NAME_LEN, "%s", name);

	inherit_attr_list(mclass, m);

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
		mem_free(m->name);
	}

	mem_free(m);

	return NULL;
}

static int
disconnect_modules_upstream(struct module *m_next, gate_idx_t igate_idx);

void destroy_module(struct module *m)
{
	int ret;

	if (m->mclass->deinit)
		m->mclass->deinit(m);

	/* disconnect from upstream modules. */
	for (int i = 0; i < m->igates.curr_size; i++) {
		ret = disconnect_modules_upstream(m, i);
		assert(ret == 0);
	}

	/* disconnect downstream modules */
	for (gate_idx_t i = 0; i < m->ogates.curr_size; i++) {
		ret = disconnect_modules(m, i);
		assert(ret == 0);
	}

	destroy_all_tasks(m);

	ret = ns_remove(m->name);
	assert(ret == 0);

	mem_free(m->name);
	mem_free(m->ogates.arr);
	mem_free(m->igates.arr);
	mem_free(m);
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

	new_arr = mem_realloc(gates->arr, sizeof(struct gate *) * new_size);
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

	ogate = mem_alloc(sizeof(struct gate));
	if (!ogate)
		return -ENOMEM;

	m_prev->ogates.arr[ogate_idx] = ogate;

	igate = m_next->igates.arr[igate_idx];
	if (!igate) {
		igate = mem_alloc(sizeof(struct gate));
		if (!igate) {
			mem_free(ogate);
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
		m_next->igates.arr[igate->gate_idx] = NULL;
		mem_free(igate);
	}

	m_prev->ogates.arr[ogate_idx] = NULL;
	mem_free(ogate);

	return 0;
}

static int
disconnect_modules_upstream(struct module *m_next, gate_idx_t igate_idx)
{
	struct gate *igate;
	struct gate *ogate;
	struct gate *ogate_next;

	if (igate_idx >= m_next->mclass->num_igates)
		return -EINVAL;

	/* no error even if the igate is unconnected already */
	if (!is_active_gate(&m_next->igates, igate_idx))
		return 0;

	igate = m_next->igates.arr[igate_idx];
	if (!igate)
		return 0;

	cdlist_for_each_entry_safe(ogate, ogate_next,
			&igate->in.ogates_upstream, out.igate_upstream)
	{
		struct module *m_prev = ogate->m;
		m_prev->ogates.arr[ogate->gate_idx] = NULL;
		mem_free(ogate);
	}

	m_next->igates.arr[igate_idx] = NULL;
	mem_free(igate);

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

	gettimeofday(&tv, NULL);

	for (int i = 0; i < batch->cnt; i++) {
		struct snbuf* pkt = batch->pkts[i];
		struct pcap_rec_hdr rec = {
			.ts_sec = tv.tv_sec,
			.ts_usec = tv.tv_usec,
			.incl_len = pkt->mbuf.data_len,
			.orig_len = pkt->mbuf.pkt_len,
		};

		struct iovec vec[2] = {{&rec, sizeof(rec)},
			{snb_head_data(pkt), snb_head_len(pkt)}};

		ret = writev(fd, vec, 2);
		if (ret < 0) {
			if (errno == EPIPE) {
				log_debug("Broken pipe: stopping tcpdump\n");
				gate->tcpdump = 0;
				gate->fifo_fd = 0;
				close(fd);
			}
			return;
		}
	}
}
#endif
