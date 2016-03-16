#include "../module.h"
#include "../port.h"

struct queue_out_priv {
	struct port *port;
	pkt_io_func_t send_pkts;
	queue_t qid;
};

static struct snobj *queue_out_init(struct module *m, struct snobj *arg)
{
	struct queue_out_priv *priv = get_priv(m);

	struct snobj *t;

	const char *port_name;

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

	ret = acquire_queues(priv->port, m, PACKET_DIR_OUT, &priv->qid, 1);
	if (ret < 0)
		return snobj_errno(-ret);

	priv->send_pkts = priv->port->driver->send_pkts;

	return NULL;
}

static void queue_out_deinit(struct module *m)
{
	struct queue_out_priv *priv = get_priv(m);

	release_queues(priv->port, m, PACKET_DIR_OUT, &priv->qid, 1);
}

static struct snobj *queue_out_get_desc(const struct module *m)
{
	const struct queue_out_priv *priv = get_priv_const(m);

	return snobj_str_fmt("%s/%s", priv->port->name, 
			priv->port->driver->name);
}

static void queue_out_process_batch(struct module *m,
				  struct pkt_batch *batch)
{
	struct queue_out_priv *priv = get_priv(m);
	struct port *p = priv->port;

	const queue_t qid = priv->qid;

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

static const struct mclass queue_out = {
	.name		= "QueueOut",
	.num_igates	= 1,
	.num_ogates	= 0,
	.priv_size	= sizeof(struct queue_out_priv),
	.init		= queue_out_init,
	.deinit		= queue_out_deinit,
	.get_desc	= queue_out_get_desc,
	.process_batch	= queue_out_process_batch,
};

ADD_MCLASS(queue_out)
