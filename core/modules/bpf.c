#include <pcap.h>
#include <stdlib.h>

#include "../module.h"
#include "../snbuf.h"
#include "../utils/histogram.h"

/* Note: bpf_filter will return SNAPLEN if matched, and 0 if unmatched. */
/* Note: unmatched packets are sent to gate 0 */
#define SNAPLEN		0xffff

struct filter {
	int priority;
	int gate;
	struct bpf_program prog;
};

struct bpf_priv {
	int n_filters;
	struct filter filters[MAX_OUTPUT_GATES];
};

static int compare_filter(const void *filter1, const void *filter2) {
	struct filter *f1 = (struct filter*)filter1;
	struct filter *f2 = (struct filter*)filter2;
	if (f1->priority > f2->priority) {
		return -1;
	} else if (f1->priority == f2->priority) {
		return 0;
	}
	return 1;
}

static struct snobj *bpf_query(struct module *, struct snobj *);

static struct snobj *bpf_init(struct module *m, struct snobj *arg)
{
	struct bpf_priv *priv = get_priv(m);
	priv->n_filters = 0;
	if (arg) {
		return bpf_query(m, arg);
	}
	return NULL;
}

static void bpf_deinit(struct module *m)
{
	struct bpf_priv *priv = get_priv(m);

	for (int i = 0; i < priv->n_filters; i++) {
		pcap_freecode(&priv->filters[i].prog);
	}
	priv->n_filters = 0;
}

static struct snobj *bpf_query(struct module *m, struct snobj *q)
{
	struct bpf_priv *priv = get_priv(m);

	if (snobj_type(q) == TYPE_STR && 
			strcmp(snobj_str_get(q), "reset") == 0) {
		bpf_deinit(m);
		return NULL;
	} else if (snobj_type(q) != TYPE_LIST)
		return snobj_err(EINVAL, "Argument must be a list");
	
	if (priv->n_filters + q->size > MAX_OUTPUT_GATES) {
		return snobj_err(EINVAL, "Too many filters");
	}
	for (int i = 0; i < q->size; i++) {
		struct snobj *f = snobj_list_get(q, i);
		int priority;
		char *filter_string;
		int gate;
		if (snobj_type(f) != TYPE_MAP) {
			return snobj_err(EINVAL, "Each filter must be a map");
		}
		if (!snobj_eval(f, "priority")) {
			return snobj_err(EINVAL, "Each filter must specify a "
					         "priority");
		}
		priority = snobj_eval_int(f, "priority");
		if (!snobj_map_get(f, "filter")) {
			return snobj_err(EINVAL, "Must specify a filter "
					         "expression");
		}
		filter_string = snobj_eval_str(f, "filter");
		if (!snobj_eval(f, "gate")) {
			return snobj_err(EINVAL, "Each filter must specify an "
					         "ouput gate");
		}
		gate = snobj_eval_int(f, "gate");
		if (gate < 0 || gate > MAX_OUTPUT_GATES) {
			return snobj_err(EINVAL, "Invalid gate");
		}

		priv->filters[priv->n_filters].priority = priority;
		priv->filters[priv->n_filters].gate = gate;
		if (pcap_compile_nopcap(SNAPLEN,
					DLT_EN10MB, 	/* Ethernet */
					&priv->filters[priv->n_filters].prog, 
					filter_string, 
					1,		/* optimize (IL only) */
					PCAP_NETMASK_UNKNOWN) == -1)
		{
			return snobj_err(EINVAL, "BPF compilation error");
		}
		priv->n_filters++;	
		qsort(priv->filters, priv->n_filters, sizeof(struct filter),
			&compare_filter);
	}
	return NULL;
}

static struct snobj *bpf_get_desc(const struct module *m)
{
	const struct bpf_priv *priv = get_priv_const(m);

	return snobj_str_fmt("Filters: %d", priv->n_filters);
}

static void bpf_process_batch(struct module *m,
		struct pkt_batch *batch)
{
	gate_t ogates[MAX_PKT_BURST];
	int i;
	struct bpf_priv *priv = get_priv(m);

	for (i = 0; i < batch->cnt; i++) {
		struct snbuf* pkt = batch->pkts[i];
		int match = 0;
		for (int filter = 0; filter < priv->n_filters && !match; 
				filter++) {
			if (bpf_filter(priv->filters[filter].prog.bf_insns,
				       (uint8_t*)snb_head_data(pkt),
				       snb_total_len(pkt),
				       snb_head_len(pkt)) != 0) {
				ogates[i] = priv->filters[filter].gate;
				match = 1;
			}
		}
		if (!match) {
			ogates[i] = 0;
		}
	}

	run_split(m, ogates, batch); 
}

static const struct mclass bpf = {
	.name 		= "BPF",
	.num_ogates	= MAX_OUTPUT_GATES,
	.priv_size	= sizeof(struct bpf_priv),
	.init 		= bpf_init,
	.deinit 	= bpf_deinit,
	.query		= bpf_query,
	.get_desc	= bpf_get_desc,
	.process_batch  = bpf_process_batch,
};

ADD_MCLASS(bpf)
