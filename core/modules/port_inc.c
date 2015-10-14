#include "../module.h"
#include "../port.h"

struct port_inc_priv {
	struct port *port;
};

static struct snobj *port_inc_init(struct module *m, struct snobj *arg)
{
	struct port_inc_priv *priv = get_priv(m);

	const char *port_name;

	port_name = snobj_str_get(arg);
	if (!port_name)
		return snobj_err(EINVAL, "Argument must be a string");

	priv->port = find_port(port_name);
	if (!priv->port)
		return snobj_err(ENODEV, "Port %s not found", port_name);

	acquire_queue(priv->port, PACKET_DIR_INC, 0 /* XXX */, m);

	task_create(m, NULL);

	return NULL;
}

static void port_inc_deinit(struct module *m)
{
	struct port_inc_priv *priv = get_priv(m);

	release_queue(priv->port, PACKET_DIR_INC, 0 /* XXX */, m);
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

	const queue_t qid = 0;	/* XXX */

	struct pkt_batch batch;
	struct task_result ret;

	uint64_t received_bytes = 0;

	const int pkt_burst = MAX_PKT_BURST;
	const int pkt_overhead = 24;

	batch.cnt = p->driver->recv_pkts(p, qid, batch.pkts, pkt_burst);

	if (batch.cnt == 0) {
		ret.packets = 0;
		ret.bits = 0;
		return ret;
	}

	for (int i = 0; i < batch.cnt; i++)
		received_bytes += snb_total_len(batch.pkts[i]);

	ret.packets = batch.cnt;
	ret.bits = (received_bytes + pkt_overhead * batch.cnt) * 8;

	p->queue_stats[PACKET_DIR_INC][qid].packets += batch.cnt;
	p->queue_stats[PACKET_DIR_INC][qid].bytes += received_bytes;

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
