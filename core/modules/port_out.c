#include "../module.h"
#include "../port.h"

struct port_out_priv {
	struct port *port;
	pkt_io_func_t send_pkts;
};

static struct snobj *port_out_init(struct module *m, struct snobj *arg)
{
	struct port_out_priv *priv = get_priv(m);

	const char *port_name;

	int ret;

	if (!arg || !(port_name = snobj_eval_str(arg, "port")))
		return snobj_err(EINVAL, "'port' must be given as a string");

	priv->port = find_port(port_name);
	if (!priv->port)
		return snobj_err(ENODEV, "Port %s not found", port_name);

	if (priv->port->num_queues[PACKET_DIR_OUT] == 0)
		return snobj_err(ENODEV, "Port %s has no outgoing queue",
				port_name);

	ret = acquire_queues(priv->port, m, PACKET_DIR_OUT, NULL, 0);
	if (ret < 0)
		return snobj_errno(-ret);

	priv->send_pkts = priv->port->driver->send_pkts;

	return NULL;
}

static void port_out_deinit(struct module *m)
{
	struct port_out_priv *priv = get_priv(m);

	release_queues(priv->port, m, PACKET_DIR_OUT, NULL, 0);
}

static struct snobj *port_out_get_desc(const struct module *m)
{
	const struct port_out_priv *priv = get_priv_const(m);

	return snobj_str_fmt("%s/%s", priv->port->name, 
			priv->port->driver->name);
}

static void port_out_process_batch(struct module *m,
				  struct pkt_batch *batch)
{
	struct port_out_priv *priv = get_priv(m);
	struct port *p = priv->port;

	/* TODO: choose appropriate out queue */
	const queue_t qid = 0;

	uint64_t sent_bytes = 0;
	int sent_pkts;

	sent_pkts = priv->send_pkts(p, qid, batch->pkts, batch->cnt);

	if (!(p->driver->flags & DRIVER_FLAG_SELF_OUT_STATS)) {
		const packet_dir_t dir = PACKET_DIR_OUT;

		for (int i = 0; i < sent_pkts; i++)
			sent_bytes += snb_total_len(batch->pkts[i]);

		p->queue_stats[dir][qid].packets += sent_pkts;
		p->queue_stats[dir][qid].dropped += (batch->cnt - sent_pkts);
		p->queue_stats[dir][qid].bytes += sent_bytes;
	}

	if (sent_pkts < batch->cnt)
		snb_free_bulk(batch->pkts + sent_pkts, batch->cnt - sent_pkts);
}

static const struct mclass port_out = {
	.name		= "PortOut",
	.num_igates	= 1,
	.num_ogates	= 0,
	.priv_size	= sizeof(struct port_out_priv),
	.init		= port_out_init,
	.deinit		= port_out_deinit,
	.get_desc	= port_out_get_desc,
	.process_batch	= port_out_process_batch,
};

ADD_MCLASS(port_out)
