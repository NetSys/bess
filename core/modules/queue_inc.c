#include "../module.h"
#include "../port.h"

struct queue_inc_priv {
	struct port *port;
	pkt_io_func_t recv_pkts;
	queue_t qid;
	int prefetch;
};

static struct snobj *queue_inc_init(struct module *m, struct snobj *arg)
{
	struct queue_inc_priv *priv = get_priv(m);

	struct snobj *t;

	const char *port_name;
	task_id_t tid;

	int ret;

	if (!arg || snobj_type(arg) != TYPE_MAP)
		return snobj_err(EINVAL, "Argument must be a map");

	t = snobj_eval(arg, "port");
	if (!t || !(port_name = snobj_str_get(t)))
		return snobj_err(EINVAL, "Field 'port' must be specified");

	t = snobj_eval(arg, "qid");
	if (!t || snobj_type(t) != TYPE_INT)
		return snobj_err(EINVAL, "Field 'qid' must be specified");
	priv->qid = snobj_uint_get(t);

	priv->port = find_port(port_name);
	if (!priv->port)
		return snobj_err(ENODEV, "Port %s not found", port_name);

	if (snobj_eval_int(arg, "prefetch"))
		priv->prefetch = 1;

	tid = register_task(m, (void *)(uint64_t)priv->qid);
	if (tid == INVALID_TASK_ID)
		return snobj_err(ENOMEM, "Task creation failed");

	ret = acquire_queues(priv->port, m, PACKET_DIR_INC, &priv->qid, 1);
	if (ret < 0)
		return snobj_errno(-ret);

	priv->recv_pkts = priv->port->driver->recv_pkts;

	return NULL;
}

static void queue_inc_deinit(struct module *m)
{
	struct queue_inc_priv *priv = get_priv(m);

	release_queues(priv->port, m, PACKET_DIR_INC, &priv->qid, 1);
}

static struct snobj *queue_inc_get_desc(const struct module *m)
{
	const struct queue_inc_priv *priv = get_priv_const(m);

	return snobj_str_fmt("%s:%hhu/%s", 
			priv->port->name, 
			priv->qid, 
			priv->port->driver->name);
}

static struct task_result
queue_inc_run_task(struct module *m, void *arg)
{
	struct queue_inc_priv *priv = get_priv(m);
	struct port *p = priv->port;

	const queue_t qid = (queue_t)(uint64_t)arg;

	struct pkt_batch batch;
	struct task_result ret;

	uint64_t received_bytes = 0;

	const int pkt_burst = MAX_PKT_BURST;
	const int pkt_overhead = 24;
	
	int cnt;

	cnt = batch.cnt = priv->recv_pkts(p, qid, batch.pkts, pkt_burst);

	if (cnt == 0) {
		ret.packets = 0;
		ret.bits = 0;
		return ret;
	}

	/* NOTE: we cannot skip this step since it might be used by scheduler */
	if (priv->prefetch) {
		for (int i = 0; i < cnt; i++) {
			received_bytes += snb_total_len(batch.pkts[i]);
			rte_prefetch0(snb_head_data(batch.pkts[i]));
		}
	} else {
		for (int i = 0; i < cnt; i++)
			received_bytes += snb_total_len(batch.pkts[i]);
	}

	ret.packets = cnt;
	ret.bits = (received_bytes + pkt_overhead * cnt) * 8;

	if (!(p->driver->flags & DRIVER_FLAG_SELF_INC_STATS)) {
		p->queue_stats[PACKET_DIR_INC][qid].packets += cnt;
		p->queue_stats[PACKET_DIR_INC][qid].bytes += received_bytes;
	}

	run_next_module(m, &batch);

	return ret;
}

static const struct mclass queue_inc = {
	.name 		= "QueueInc",
	.priv_size	= sizeof(struct queue_inc_priv),
	.init 		= queue_inc_init,
	.deinit		= queue_inc_deinit,
	.get_desc	= queue_inc_get_desc,
	.run_task 	= queue_inc_run_task,
};

ADD_MCLASS(queue_inc)
