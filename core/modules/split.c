#include <rte_byteorder.h>

#include "../module.h"

#define MAX_SIZE	8

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
  #error this code assumes little endian architecture (x86)
#endif

struct split_priv {
	uint64_t mask;
	int attr_id;
	int offset;
	int size;
};

static struct snobj *split_init(struct module *m, struct snobj *arg)
{
	struct split_priv *priv = get_priv(m);

	if (!arg || snobj_type(arg) != TYPE_MAP)
		return snobj_err(EINVAL, "specify 'offset'/'name' and 'size'");

	priv->size = snobj_eval_uint(arg, "size");
	if (priv->size < 1 || priv->size > MAX_SIZE)
		return snobj_err(EINVAL, "'size' must be 1-%d", MAX_SIZE);

	priv->mask = (1ul << (priv->size * 8)) - 1;

	const char *name = snobj_eval_str(arg, "name");

	if (name) {
		priv->attr_id = add_metadata_attr(m, name, priv->size, MT_READ);
		if (priv->attr_id < 0)
			return snobj_err(-priv->attr_id,
					"add_metadata_attr() failed");
	} else if (snobj_eval_exists(arg, "offset")) {
		priv->attr_id = -1;
		priv->offset = snobj_eval_int(arg, "offset");
		if (priv->offset < 0 || priv->offset > 1024)
			return snobj_err(EINVAL, "invalid 'offset'");
		priv->offset -= (8 - priv->size);
	} else
		return snobj_err(EINVAL, "must specify 'offset' or 'name'");

	return NULL;
}

static void split_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct split_priv *priv = get_priv(m);
	gate_idx_t ogate[MAX_PKT_BURST];
	int cnt = batch->cnt;

	if (priv->attr_id >= 0) {
		int attr_id = priv->attr_id;

		for (int i = 0; i < cnt; i++) {
			struct snbuf *pkt = batch->pkts[i];

			uint64_t val = get_attr(m, attr_id, pkt, uint64_t);
			val &= priv->mask;

			if (is_valid_gate(val))
				ogate[i] = val;
			else
				ogate[i] = DROP_GATE;
		}
	} else {
		int offset = priv->offset;

		for (int i = 0; i < cnt; i++) {
			struct snbuf *pkt = batch->pkts[i];
			char *head = snb_head_data(pkt);

			uint64_t val = *(uint64_t *)(head + offset);
			val = rte_be_to_cpu_64(val) & priv->mask;

			if (is_valid_gate(val))
				ogate[i] = val;
			else
				ogate[i] = DROP_GATE;
		}
	}

	run_split(m, ogate, batch);
}

static const struct mclass split = {
	.name 			= "Split",
	.help			=
		"split packets depending on packet data or metadata attributes",
	.def_module_name 	= "split",
	.num_igates		= 1,
	.num_ogates		= MAX_GATES,
	.init			= split_init,
	.process_batch  	= split_process_batch,
};

ADD_MCLASS(split)
