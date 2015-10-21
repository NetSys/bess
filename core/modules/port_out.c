#include "../module.h"
#include "../port.h"

struct port_out_priv {
	struct port *port;
};

static struct snobj *port_out_init(struct module *m, struct snobj *arg)
{
	struct port_out_priv *priv = get_priv(m);

	const char *port_name;

	if (!arg || !(port_name = snobj_str_get(arg)))
		return snobj_err(EINVAL, "Argument must be a port name " \
				"(string)");

	priv->port = find_port(port_name);
	if (!priv->port)
		return snobj_err(ENODEV, "Port %s not found", port_name);

	acquire_queue(priv->port, PACKET_DIR_OUT, 0 /* XXX */, m);

	return NULL;
}

static void port_out_deinit(struct module *m)
{
	struct port_out_priv *priv = get_priv(m);

	release_queue(priv->port, PACKET_DIR_OUT, 0 /* XXX */, m);
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

	const queue_t qid = 0;	/* XXX */

	uint64_t sent_bytes = 0;
	int sent_pkts;

	sent_pkts = p->driver->send_pkts(p, qid, batch->pkts, batch->cnt);

	for (int i = 0; i < sent_pkts; i++)
		sent_bytes += snb_total_len(batch->pkts[i]);

	p->queue_stats[PACKET_DIR_OUT][qid].packets += sent_pkts;
	p->queue_stats[PACKET_DIR_OUT][qid].dropped += (batch->cnt - sent_pkts);
	p->queue_stats[PACKET_DIR_OUT][qid].bytes += sent_bytes;

	if (sent_pkts < batch->cnt)
		snb_free_bulk(batch->pkts + sent_pkts, batch->cnt - sent_pkts);
}

static const struct mclass port_out = {
	.name		= "PortOut",
	.priv_size	= sizeof(struct port_out_priv),
	.init		= port_out_init,
	.deinit		= port_out_deinit,
	.get_desc	= port_out_get_desc,
	.process_batch	= port_out_process_batch,
};

ADD_MCLASS(port_out)
