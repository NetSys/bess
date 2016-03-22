#include "../module.h"

/* XXX: currently doesn't support multiple workers */
struct roundrobin_priv {
	gate_idx_t gates[MAX_GATES];
	uint32_t ngates;
	uint32_t current_gate;
	uint8_t batch_mode;
};

static struct snobj *roundrobin_init(struct module *m, struct snobj *arg)
{	
	struct roundrobin_priv* priv = get_priv(m);
	if (!arg)
		return snobj_err(EINVAL, "Must specify arguments");

	priv->batch_mode = (snobj_eval_int(arg, "packet_mode") == 0);

	priv->current_gate = 0;

	if (snobj_eval_exists(arg, "gates") &&
	      snobj_eval(arg, "gates")->type == TYPE_INT) {
		int gate = snobj_eval_int(arg, "gates");
		if (gate > MAX_GATES)
			return snobj_err(EINVAL, "No more than %d gates", 
					MAX_GATES);
		priv->ngates = gate;
		for (int i = 0; i < gate; i++) {
			priv->gates[i] = i;
		}
	} else if (snobj_eval_exists(arg, "gates") &&
		   snobj_eval(arg, "gates")->type == TYPE_LIST) {
		struct snobj *gates = snobj_eval(arg, "gates");
		if (gates->size > MAX_GATES)
			return snobj_err(EINVAL, "No more than %d gates", 
					MAX_GATES);

		priv->ngates = gates->size;

		for (int i = 0; i < gates->size; i++) {
			priv->gates[i] = 
				snobj_int_get(snobj_list_get(gates, i));
			if (priv->gates[i] > MAX_GATES)
				return snobj_err(EINVAL, "Invalid gate %d",
						priv->gates[i]);
		}
	} else {
		return snobj_err(EINVAL, "Must specify gates to round robin");
	}
	return NULL;
	
}

static struct snobj *roundrobin_query(struct module *m, struct snobj *arg)
{
	struct roundrobin_priv* priv = get_priv(m);

	if (snobj_eval_exists(arg, "packet_mode"))
		priv->batch_mode = (snobj_eval_int(arg, "packet_mode") == 0);

	if (snobj_eval_exists(arg, "gates")) {
		int gate = snobj_eval_int(arg, "gates");
		if (gate > MAX_GATES)
			return snobj_err(EINVAL, "No more than %d gates", 
					MAX_GATES);
		priv->ngates = gate;
		for (int i = 0; i < gate; i++) {
			priv->gates[i] = i;
		}
	} else if (snobj_eval_exists(arg, "gate_list")) {
		struct snobj *gates = snobj_eval(arg, "gate_list");
		if (gates->size > MAX_GATES)
			return snobj_err(EINVAL, "No more than %d gates", 
					MAX_GATES);

		for (int i = 0; i < gates->size; i++) {
			priv->gates[i] = 
				snobj_int_get(snobj_list_get(gates, i));
			if (priv->gates[i] > MAX_GATES)
				return snobj_err(EINVAL, "Invalid gate %d",
						priv->gates[i]);
		}
	}

	return NULL;	
}

static void
roundrobin_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct roundrobin_priv* priv = get_priv(m);
	gate_idx_t ogates[MAX_PKT_BURST];
	if (priv->batch_mode) {
		gate_idx_t gate = priv->gates[priv->current_gate];
		priv->current_gate = (priv->current_gate + 1) % priv->ngates;
		run_choose_module(m, gate, batch);
	} else {
		for (int i = 0; i < batch->cnt; i++) {
			ogates[i] = priv->gates[priv->current_gate];
			priv->current_gate = (priv->current_gate + 1) % 
						priv->ngates;
		}
		run_split(m, ogates, batch);
	}
}

static const struct mclass roundrobin = {
	.name 		= "Roundrobin",
	.num_igates	= 1,
	.num_ogates	= MAX_GATES,
	.priv_size	= sizeof(struct roundrobin_priv),
	.init 		= roundrobin_init,
	.process_batch 	= roundrobin_process_batch,
	.query		= roundrobin_query,
};

ADD_MCLASS(roundrobin)
