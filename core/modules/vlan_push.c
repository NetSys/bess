#include <string.h>

#include <arpa/inet.h>

#include "../module.h"

struct vlan_push_priv {
	uint32_t vlan_tag;	/* network order */
};

static struct snobj *query(struct module *m, struct snobj *q);

static struct snobj *init(struct module *m, struct snobj *arg)
{
	return query(m, arg);
}

static struct snobj *query(struct module *m, struct snobj *q)
{
	struct vlan_push_priv *priv = get_priv(m);
	uint16_t tci;

	if (!q || snobj_type(q) != TYPE_INT)
		return snobj_err(EINVAL, "TCI must be given as an integer");

	tci = snobj_uint_get(q);

	priv->vlan_tag = htonl((0x8100 << 16) | tci);

	return NULL;
}

static struct snobj *get_desc(const struct module *m)
{
	const struct vlan_push_priv *priv = get_priv_const(m);
	uint32_t vlan_tag_cpu = ntohl(priv->vlan_tag);

	return snobj_str_fmt("PCP=%u DEI=%u VID=%u",
			(vlan_tag_cpu >> 13) & 0x0007,
			(vlan_tag_cpu >> 12) & 0x0001,
			vlan_tag_cpu & 0x0fff);
}

static void process_batch(struct module *m, struct pkt_batch *batch)
{
	struct vlan_push_priv *priv = get_priv(m);

	for (int i = 0; i < batch->cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		char *ptr = snb_head_data(pkt);

		memmove(ptr - 4, ptr, 12);
		*(uint32_t *)(ptr + 8) = priv->vlan_tag;
		snb_prepend(pkt, 4);
	}
		
	run_next_module(m, batch);
}

static const struct mclass vlan_push = {
	.name 			= "VLANPush",
	.def_module_name 	= "vlan_push",
	.priv_size		= sizeof(struct vlan_push_priv),
	.init 			= init,
	.query			= query,
	.get_desc		= get_desc,
	.process_batch  	= process_batch,
};

ADD_MCLASS(vlan_push)
