#include <pcap.h>

#include "../module.h"
#include "../snbuf.h"
#include "../utils/histogram.h"

/* Note: bpf_filter will return SNAPLEN if matched, and 0 if unmatched. */
#define SNAPLEN		0xffff

struct bpf_priv {
	char filter_exp[1024];
	struct bpf_program fp;
};

static struct snobj *bpf_query(struct module *, struct snobj *);

static struct snobj *bpf_init(struct module *m, struct snobj *arg)
{
	if (!arg) {
		struct snobj *empty_str;
		struct snobj *ret;

		empty_str = snobj_str("");
		ret = bpf_query(m, empty_str);
		snobj_free(empty_str);
		return ret;
	}
		return bpf_query(m, arg);
}

static void bpf_deinit(struct module *m)
{
	struct bpf_priv *priv = get_priv(m);

	pcap_freecode(&priv->fp);
}

static struct snobj *bpf_query(struct module *m, struct snobj *q)
{
	struct bpf_priv *priv = get_priv(m);

	if (snobj_type(q) != TYPE_STR)
		return snobj_err(EINVAL, "Argument must be a list");

	strcpy(priv->filter_exp, snobj_str_get(q));

	if (pcap_compile_nopcap(SNAPLEN,
				DLT_EN10MB, 	/* Ethernet */
				&priv->fp, 
				priv->filter_exp, 
				1,		/* optimize (IL only) */
				PCAP_NETMASK_UNKNOWN) == -1)
	{
		return snobj_err(EINVAL, "BPF compilation error");
	}

	return NULL;
}

static struct snobj *bpf_get_desc(const struct module *m)
{
	const struct bpf_priv *priv = get_priv_const(m);

	return snobj_str_fmt("%s", priv->filter_exp);
}

static void bpf_process_batch(struct module *m,
		struct pkt_batch *batch)
{
	gate_t ogates[MAX_PKT_BURST];
	int i;
	struct bpf_priv *priv = get_priv(m);

	for (i = 0; i < batch->cnt; i++) {
		struct snbuf* pkt = batch->pkts[i];
		int ret;
		int out_gate;

		ret = bpf_filter(priv->fp.bf_insns, 
				(uint8_t *)snb_head_data(pkt), 
				snb_total_len(pkt), 
				snb_head_len(pkt));

		ogates[i] = (ret == 0);
	}

	run_split(m, ogates, batch); 
}

static const struct mclass bpf = {
	.name 		= "BPF",
	.priv_size	= sizeof(struct bpf_priv),
	.init 		= bpf_init,
	.deinit 	= bpf_deinit,
	.query		= bpf_query,
	.get_desc	= bpf_get_desc,
	.process_batch  = bpf_process_batch,
};

ADD_MCLASS(bpf)
