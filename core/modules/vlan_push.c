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

	if (!q || snobj_type(q) != TYPE_INT)
		return snobj_err(EINVAL, "TCI must be given as an integer");

	tci = snobj_uint_get(q);

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
#if __SSE2__
static void vpush_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct vlan_push_priv *priv = get_priv(m);
	int cnt = batch->cnt;

	uint32_t tag[2] = {priv->qinq_tag, priv->vlan_tag};

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		char *ptr = snb_head_data(pkt);
		__m128i mac_addr;
		uint16_t tpid;

		pkt->mbuf.data_off -= 4;
		pkt->mbuf.data_len += 4;
		pkt->mbuf.pkt_len += 4;

		/* shift 12 bytes to the left by 4 bytes */
		mac_addr = _mm_loadu_si128((__m128i *)ptr);
		tpid = _mm_extract_epi16(mac_addr, 6);

		mac_addr = _mm_insert_epi32(mac_addr, 
				tag[tpid == rte_cpu_to_be_16(0x8100)], 
				3);

		_mm_storeu_si128((__m128i *)(ptr - 4), mac_addr);
	}
		
	run_next_module(m, batch);
}
#else
static void vpush_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct vlan_push_priv *priv = get_priv(m);
	int cnt = batch->cnt;

	uint32_t qinq_tag = priv->qinq_tag;
	uint32_t vlan_tag = priv->vlan_tag;

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		char *ptr = snb_head_data(pkt);
		uint16_t tpid = *((uint16_t *)(ptr + 12);

		pkt->mbuf.data_off -= 4;
		pkt->mbuf.data_len += 4;
		pkt->mbuf.pkt_len += 4;

		tpid = *((uint16_t *)(ptr + 12));
		memmove(ptr - 4, ptr, 12);

		/* already tagged? */
		if (tpid == rte_cpu_to_be_16(0x8100))
			*(uint32_t *)(ptr + 8) = qinq_tag;
		else
			*(uint32_t *)(ptr + 8) = vlan_tag;
	}
		
	run_next_module(m, batch);
}
#endif

static const struct mclass vlan_push = {
	.name 			= "VLANPush",
	.def_module_name 	= "vlan_push",
	.priv_size		= sizeof(struct vlan_push_priv),
	.init 			= vpush_init,
	.query			= vpush_query,
	.get_desc		= vpush_get_desc,
	.process_batch  	= vpush_process_batch,
};

ADD_MCLASS(vlan_push)
