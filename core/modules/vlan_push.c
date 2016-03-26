#include <string.h>

#include <arpa/inet.h>

#include "../module.h"
#include "../utils/simd.h"

struct vlan_push_priv {
	/* network order */
	uint32_t vlan_tag;	
	uint32_t qinq_tag;
};

static struct snobj *vpush_query(struct module *m, struct snobj *q);

static struct snobj *vpush_init(struct module *m, struct snobj *arg)
{
	return vpush_query(m, arg);
}

static struct snobj *vpush_query(struct module *m, struct snobj *q)
{
	struct vlan_push_priv *priv = get_priv(m);
	uint16_t tci;

	if (!q || !snobj_eval_exists(q, "tci"))
		return snobj_err(EINVAL, "'tci' must be given as an integer");

	tci = snobj_eval_uint(q, "tci");

	if (tci > 0xffff)
		return snobj_err(EINVAL, "'tci' value should be 0-65535");

	priv->vlan_tag = htonl((0x8100 << 16) | tci);
	priv->qinq_tag = htonl((0x88a8 << 16) | tci);

	return NULL;
}

static struct snobj *vpush_get_desc(const struct module *m)
{
	const struct vlan_push_priv *priv = get_priv_const(m);
	uint32_t vlan_tag_cpu = ntohl(priv->vlan_tag);

	return snobj_str_fmt("PCP=%u DEI=%u VID=%u",
			(vlan_tag_cpu >> 13) & 0x0007,
			(vlan_tag_cpu >> 12) & 0x0001,
			vlan_tag_cpu & 0x0fff);
}

/* the behavior is undefined if a packet is already double tagged */
static void vpush_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct vlan_push_priv *priv = get_priv(m);
	int cnt = batch->cnt;

	uint32_t vlan_tag = priv->vlan_tag;
	uint32_t qinq_tag = priv->qinq_tag;

	//uint32_t tag[2] = {vlan_tag, qinq_tag};

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		char *new_head;
		uint16_t tpid;

		if ((new_head = snb_prepend(pkt, 4)) != NULL) {
			/* shift 12 bytes to the left by 4 bytes */
#if __SSE4_1__
			__m128i ethh;

			ethh = _mm_loadu_si128((__m128i *)(new_head + 4));
			tpid = _mm_extract_epi16(ethh, 6);

			ethh = _mm_insert_epi32(ethh, 
					(tpid == rte_cpu_to_be_16(0x8100)) ?
						qinq_tag : vlan_tag,
					3);

			_mm_storeu_si128((__m128i *)new_head, ethh);
#else
			tpid = *(uint16_t *)(new_head + 16);
			memmove(new_head, new_head + 4, 12);

			*(uint32_t *)(new_head + 12) = 
					(tpid == rte_cpu_to_be_16(0x8100)) ?
						qinq_tag : vlan_tag;
#endif
		}
	}
		
	run_next_module(m, batch);
}

static const struct mclass vlan_push = {
	.name 			= "VLANPush",
	.def_module_name 	= "vlan_push",
	.num_igates 		= 1,
	.num_ogates		= 1,
	.priv_size		= sizeof(struct vlan_push_priv),
	.init 			= vpush_init,
	.query			= vpush_query,
	.get_desc		= vpush_get_desc,
	.process_batch  	= vpush_process_batch,
};

ADD_MCLASS(vlan_push)
