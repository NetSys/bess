#include "../module.h"

#define SLOTS			(MAX_PKT_BURST * 2 - 1)
#define MAX_TEMPLATE_SIZE	1536

struct rewrite_priv {
	/* For fair round robin we remember the next index for later.
	 * [0, num_templates - 1] */
	int next_turn;

	int num_templates;
	uint16_t template_size[SLOTS];
	unsigned char templates[SLOTS][MAX_TEMPLATE_SIZE] __ymm_aligned;
};

static struct snobj *rewrite_query(struct module *, struct snobj *);

static struct snobj *rewrite_init(struct module *m, struct snobj *arg)
{
	if (arg)
		return rewrite_query(m, arg);
	
	return NULL;
}

/* TODO: make this atomic */

static struct snobj *handle_templates(struct rewrite_priv *priv, 
		struct snobj *templates)
{
	int i;

	if (snobj_type(templates) != TYPE_LIST)
		return snobj_err(EINVAL, "'templates' must be a list");

	if (templates->size > MAX_PKT_BURST)
		return snobj_err(EINVAL, "Max %d packet templates " \
				"can be specified", MAX_PKT_BURST);

	priv->next_turn = 0;
	priv->num_templates = 0;

	for (i = 0; i < templates->size; i++) {
		struct snobj *template = snobj_list_get(templates, i);

		if (template->type != TYPE_BLOB)
			return snobj_err(EINVAL, "Packet template " \
					"should be BLOB type");

		if (template->size > MAX_TEMPLATE_SIZE)
			return snobj_err(EINVAL, "Template is too big");

		memset(priv->templates[i], 0, MAX_TEMPLATE_SIZE);
		memcpy(priv->templates[i], snobj_blob_get(template), 
				template->size);
		priv->template_size[i] = template->size;
	}

	for (i = templates->size; i < SLOTS; i++) {
		int j = i % templates->size;
		memcpy(priv->templates[i], priv->templates[j], 
				priv->template_size[j]);
		priv->template_size[i] = priv->template_size[j];
	}

	priv->num_templates = templates->size;

	return NULL;
}

static struct snobj *rewrite_query(struct module *m, struct snobj *q)
{
	struct rewrite_priv *priv = get_priv(m);

	struct snobj *templates = snobj_eval(q, "templates");

	struct snobj *err;

	if (templates) {
		err = handle_templates(priv, templates);
		if (err)
			return err;
	}

	return NULL;
}

static void rewrite_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct rewrite_priv *priv = get_priv(m);

	if (priv->num_templates) {
		int start = priv->next_turn;
		const int cnt = batch->cnt;

		for (int i = 0; i < cnt; i++) {
			struct snbuf *snb = batch->pkts[i];
			char *ptr = snb_head_data(snb);
			uint16_t size = priv->template_size[start + i];

			snb->mbuf.pkt_len = snb->mbuf.data_len = size;
			rte_memcpy(ptr, priv->templates[start + i], size);
		}

		priv->next_turn = (start + cnt) % priv->num_templates;
	}

	run_next_module(m, batch);
}

static const struct mclass rewrite = {
	.name 			= "Rewrite",
	.priv_size		= sizeof(struct rewrite_priv),
	.init 			= rewrite_init,
	.query			= rewrite_query,
	.process_batch 		= rewrite_process_batch,
};

ADD_MCLASS(rewrite)

