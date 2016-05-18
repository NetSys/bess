#include "../module.h"

#define MAX_RR_GATES	16384

/* XXX: currently doesn't support multiple workers */
struct roundrobin_priv {
	gate_idx_t gates[MAX_RR_GATES];
	int ngates;
	int current_gate;
	int per_packet;
};

static struct snobj *
command_set_mode(struct module *m, const char *cmd, struct snobj *arg)
{
	struct roundrobin_priv *priv = get_priv(m);

	const char *mode = snobj_str_get(arg);

	if (!mode)
		return snobj_err(EINVAL, "argument must be a string");

	if (strcmp(mode, "packet") == 0)
		priv->per_packet = 1;
	else if (strcmp(mode, "batch") == 0)
		priv->per_packet = 0;
	else
		return snobj_err(EINVAL, 
				"argument must be either 'packet' or 'batch'");

	return NULL;
}

static struct snobj *
command_set_gates(struct module *m, const char *cmd, struct snobj *arg)
{
	struct roundrobin_priv *priv = get_priv(m);

	if (snobj_type(arg) == TYPE_INT) {
		int gates = snobj_int_get(arg);

		if (gates < 0 || gates > MAX_RR_GATES || gates > MAX_GATES)
			return snobj_err(EINVAL, "no more than %d gates", 
					MIN(MAX_RR_GATES, MAX_GATES));

		priv->ngates = gates;
		for (int i = 0; i < gates; i++)
			priv->gates[i] = i;

	} else if (snobj_type(arg) == TYPE_LIST) {
		struct snobj *gates = arg;

		if (gates->size > MAX_RR_GATES)
			return snobj_err(EINVAL, "no more than %d gates", 
					MAX_RR_GATES);

		for (int i = 0; i < gates->size; i++) {
			struct snobj *elem = snobj_list_get(gates, i);

			if (snobj_type(elem) != TYPE_INT)
				return snobj_err(EINVAL, 
						"'gate' must be an integer");

			priv->gates[i] = snobj_int_get(elem);
			if (!is_valid_gate(priv->gates[i]))
				return snobj_err(EINVAL, "invalid gate %d",
						priv->gates[i]);
		}

		priv->ngates = gates->size;

	} else
		return snobj_err(EINVAL, "argument must specify a gate "
				"or a list of gates");

	return NULL;
}

static struct snobj *roundrobin_init(struct module *m, struct snobj *arg)
{	
	if (arg)
		return command_set_gates(m, NULL, arg);
	else
		return snobj_err(EINVAL, "argument must specify a gate "
				"or a list of gates");
}

static void
roundrobin_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct roundrobin_priv* priv = get_priv(m);
	gate_idx_t ogates[MAX_PKT_BURST];

	if (priv->per_packet) {
		for (int i = 0; i < batch->cnt; i++) {
			ogates[i] = priv->gates[priv->current_gate];
			priv->current_gate = (priv->current_gate + 1) % 
						priv->ngates;
		}
		run_split(m, ogates, batch);
	} else {
		gate_idx_t gate = priv->gates[priv->current_gate];
		priv->current_gate = (priv->current_gate + 1) % priv->ngates;
		run_choose_module(m, gate, batch);
	}
}

static const struct mclass roundrobin = {
	.name 		= "Roundrobin",
	.help		= "splits packets evenly with round robin",
	.num_igates	= 1,
	.num_ogates	= MAX_GATES,
	.priv_size	= sizeof(struct roundrobin_priv),
	.init 		= roundrobin_init,
	.process_batch 	= roundrobin_process_batch,
	.commands	 = {
		{"set_mode",	command_set_mode},
		{"set_gates",	command_set_gates},
	}
};

ADD_MCLASS(roundrobin)
