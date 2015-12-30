#include "../module.h"
#include "../port.h"

struct port_inc_priv {
	struct port *port;
	pkt_io_func_t recv_pkts;
	int prefetch;
};

static struct snobj *port_inc_init(struct module *m, struct snobj *arg)
{
	struct port_inc_priv *priv = get_priv(m);

	const char *port_name;
	queue_t num_inc_q;

	int ret;

	if (!arg || !(port_name = snobj_eval_str(arg, "port")))
		return snobj_err(EINVAL, "'port' must be given as a string");

	priv->port = find_port(port_name);
	if (!priv->port)
		return snobj_err(ENODEV, "Port %s not found", port_name);

	num_inc_q = priv->port->num_queues[PACKET_DIR_INC];
	if (num_inc_q == 0)
		return snobj_err(ENODEV, "Port %s has no incoming queue",
				port_name);

	for (queue_t qid = 0; qid < num_inc_q; qid++) { 
		task_id_t tid = register_task(m, (void *)(uint64_t)qid);

		if (tid == INVALID_TASK_ID)
			return snobj_err(ENOMEM, "Task creation failed");
	}

	if (snobj_eval_int(arg, "prefetch"))
		priv->prefetch = 1;

	ret = acquire_queues(priv->port, m, PACKET_DIR_INC, NULL, 0);
	if (ret < 0)
		return snobj_errno(-ret);

	priv->recv_pkts = priv->port->driver->recv_pkts;

	return NULL;
}

static void port_inc_deinit(struct module *m)
{
	struct port_inc_priv *priv = get_priv(m);

	release_queues(priv->port, m, PACKET_DIR_INC, NULL, 0);
}

static struct snobj *port_inc_get_desc(const struct module *m)
{
	const struct port_inc_priv *priv = get_priv_const(m);

	return snobj_str_fmt("%s/%s", priv->port->name, priv->port->driver->name);
}

static struct task_result
port_inc_run_task(struct module *m, void *arg)
{
	struct port_inc_priv *priv = get_priv(m);
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

static const struct mclass port_inc = {
	.name 		= "PortInc",
	.priv_size	= sizeof(struct port_inc_priv),
	.init 		= port_inc_init,
	.deinit		= port_inc_deinit,
	.get_desc	= port_inc_get_desc,
	.run_task 	= port_inc_run_task,
};

ADD_MCLASS(port_inc)
