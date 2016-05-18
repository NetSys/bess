#include "../module.h"
#include "../utils/random.h"

#include <rte_hash_crc.h>

#define MAX_HLB_GATES	16384

/* TODO: add symmetric mode (e.g., LB_L4_SYM), v6 mode, etc. */
enum lb_mode_t {
	LB_L2,		/* dst MAC + src MAC */
	LB_L3,		/* src IP + dst IP */
	LB_L4		/* L4 proto + src IP + dst IP + src port + dst port */
};

const enum lb_mode_t default_mode = LB_L4;

struct hlb_priv {
	gate_idx_t gates[MAX_HLB_GATES];
	int num_gates;
	enum lb_mode_t mode;
};

static struct snobj *
command_set_mode(struct module *m, const char *cmd, struct snobj *arg)
{
	struct hlb_priv *priv = get_priv(m);

	const char *mode = snobj_str_get(arg);
	
	if (!mode)
		return snobj_err(EINVAL, "argument must be a string");

	if (strcmp(mode, "l2") == 0)
		priv->mode = LB_L2;
	else if (mode && strcmp(mode, "l3") == 0)
		priv->mode = LB_L3;
	else if (mode && strcmp(mode, "l4") == 0)
		priv->mode = LB_L4;
	else
		return snobj_err(EINVAL, "available LB modes: l2, l3, l4");

	return NULL;
}

static struct snobj *
command_set_gates(struct module *m, const char *cmd, struct snobj *arg)
{
	struct hlb_priv *priv = get_priv(m);

	if (snobj_type(arg) == TYPE_INT) {
		int gates = snobj_int_get(arg);

		if (gates < 0 || gates > MAX_HLB_GATES || gates > MAX_GATES)
			return snobj_err(EINVAL, "no more than %d gates", 
					MIN(MAX_HLB_GATES, MAX_GATES));

		priv->num_gates = gates;
		for (int i = 0; i < gates; i++)
			priv->gates[i] = i;

	} else if (snobj_type(arg) == TYPE_LIST) {
		struct snobj *gates = arg;

		if (gates->size > MAX_HLB_GATES)
			return snobj_err(EINVAL, "no more than %d gates", 
					MAX_HLB_GATES);

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

		priv->num_gates = gates->size;

	} else
		return snobj_err(EINVAL, "argument must specify a gate "
				"or a list of gates");

	return NULL;
}

static struct snobj *hlb_init(struct module *m, struct snobj *arg)
{
	struct hlb_priv *priv = get_priv(m);

	priv->mode = default_mode;

	if (arg)
		return command_set_gates(m, NULL, arg);
	else
		return snobj_err(EINVAL, "argument must specify a gate "
				"or a list of gates");
}

static inline uint32_t hash_64(uint64_t val, uint32_t init_val)
{
#if __SSE4_2__
		return crc32c_sse42_u64(val, init_val);
#else
		return crc32c_2words(val, init_val);
#endif
}
/* Returns a value in [0, range) as a function of an opaque number. 
 * Also see utils/random.h */
static inline uint16_t hash_range(uint32_t hashval, uint16_t range)
{
#if 1
	union {
		uint64_t i;
		double d;
	} tmp;

	/* the resulting number is 1.(b0)(b1)..(b31)00000..00 */
	tmp.i = 0x3ff0000000000000ul | ((uint64_t)hashval << 20);

	return (tmp.d - 1.0) * range;
#else
	/* This IDIV instruction is significantly slower */
	return hashval % range;
#endif
}

static void 
lb_l2(struct hlb_priv *priv, struct pkt_batch *batch, gate_idx_t *ogates)
{
	for (int i = 0; i < batch->cnt; i++) {
		struct snbuf *snb = batch->pkts[i];
		char *head = snb_head_data(snb);
	
		uint64_t v0 = *((uint64_t *)head);
		uint32_t v1 = *((uint32_t *)(head + 8));

		uint32_t hash_val = hash_64(v0, v1);

		ogates[i] = priv->gates[hash_range(hash_val, priv->num_gates)];
	}
}

static void
lb_l3(struct hlb_priv *priv, struct pkt_batch *batch, gate_idx_t *ogates)
{
	/* assumes untagged packets */
	const int ip_offset = 14;

	for (int i = 0; i < batch->cnt; i++) {
		struct snbuf *snb = batch->pkts[i];
		char *head = snb_head_data(snb);
	
		uint32_t hash_val;
		uint64_t v = *((uint64_t *)(head + ip_offset + 12));

		hash_val = hash_64(v, 0);

		ogates[i] = priv->gates[hash_range(hash_val, priv->num_gates)];
	}
}

static void
lb_l4(struct hlb_priv *priv, struct pkt_batch *batch, gate_idx_t *ogates)
{
	/* assumes untagged packets without IP options */
	const int ip_offset = 14;
	const int l4_offset = ip_offset + 20;

	for (int i = 0; i < batch->cnt; i++) {
		struct snbuf *snb = batch->pkts[i];
		char *head = snb_head_data(snb);
	
		uint32_t hash_val;
		uint64_t v0 = *((uint64_t *)(head + ip_offset + 12));
		uint32_t v1 = *((uint64_t *)(head + l4_offset)); /* ports */

		v1 ^= *((uint8_t *)(head + ip_offset + 9));	/* ip_proto */

		hash_val = hash_64(v0, v1);

		ogates[i] = priv->gates[hash_range(hash_val, priv->num_gates)];
	}
}

static void
hlb_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct hlb_priv* priv = get_priv(m);
	gate_idx_t ogates[MAX_PKT_BURST];

	switch (priv->mode) {
	case LB_L2:
		lb_l2(priv, batch, ogates);
		break;

	case LB_L3:
		lb_l3(priv, batch, ogates);
		break;

	case LB_L4:
		lb_l4(priv, batch, ogates);
		break;

	default:
		assert(0);
	}

	run_split(m, ogates, batch);
}

static const struct mclass hlb = {
	.name 		= "HashLB",
	.help		= 
		"splits packets on a flow basis with L2/L3/L4 header fields",
	.num_igates	= 1,
	.num_ogates	= MAX_GATES,
	.priv_size	= sizeof(struct hlb_priv),
	.init 		= hlb_init,
	.process_batch 	= hlb_process_batch,
	.commands	 = {
		{"set_mode",	command_set_mode},
		{"set_gates",	command_set_gates},
	}
};

ADD_MCLASS(hlb)
