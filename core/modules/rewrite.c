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

static struct snobj *
command_add(struct module *m, const char *cmd, struct snobj *arg);

static struct snobj *rewrite_init(struct module *m, struct snobj *arg)
{
	if (arg)
		return command_add(m, NULL, arg);

	return NULL;
}

static inline void
do_rewrite_single(struct rewrite_priv *priv, struct pkt_batch *batch)
{
	const int cnt = batch->cnt;
	uint16_t size = priv->template_size[0];
	void *template = priv->templates[0];

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = batch->pkts[i];
		char *ptr = pkt->mbuf.buf_addr + SNBUF_HEADROOM;

		pkt->mbuf.data_off = SNBUF_HEADROOM;
		pkt->mbuf.pkt_len = size;
		pkt->mbuf.data_len = size;

		memcpy_sloppy(ptr, template, size);
	}
}

static inline void
do_rewrite(struct rewrite_priv *priv, struct pkt_batch *batch)
{
	int start = priv->next_turn;
	const int cnt = batch->cnt;

	for (int i = 0; i < cnt; i++) {
		uint16_t size = priv->template_size[start + i];
		struct snbuf *pkt = batch->pkts[i];
		char *ptr = pkt->mbuf.buf_addr + SNBUF_HEADROOM;

		pkt->mbuf.data_off = SNBUF_HEADROOM;
		pkt->mbuf.pkt_len = size;
		pkt->mbuf.data_len = size;

		memcpy_sloppy(ptr, priv->templates[start + i], size);
	}

	priv->next_turn = (start + cnt) % priv->num_templates;
}

static void rewrite_process_batch(struct module *m, struct pkt_batch *batch)
{
	struct rewrite_priv *priv = get_priv(m);

	if (priv->num_templates == 1)
		do_rewrite_single(priv, batch);
	else if (priv->num_templates > 1)
		do_rewrite(priv, batch);

	run_next_module(m, batch);
}

static struct snobj *
command_add(struct module *m, const char *cmd, struct snobj *arg)
{
	struct rewrite_priv *priv = get_priv(m);

	int curr = priv->num_templates;
	int i;

	if (snobj_type(arg) != TYPE_LIST)
		return snobj_err(EINVAL, "argument must be a list");

	if (curr + arg->size > MAX_PKT_BURST)
		return snobj_err(EINVAL, "max %d packet templates " \
				"can be used", MAX_PKT_BURST);

	for (i = 0; i < arg->size; i++) {
		struct snobj *template = snobj_list_get(arg, i);

		if (template->type != TYPE_BLOB)
			return snobj_err(EINVAL, "packet template " \
					"should be BLOB type");

		if (template->size > MAX_TEMPLATE_SIZE)
			return snobj_err(EINVAL, "template is too big");

		memset(priv->templates[curr + i], 0, MAX_TEMPLATE_SIZE);
		memcpy(priv->templates[curr + i], snobj_blob_get(template),
				template->size);
		priv->template_size[curr + i] = template->size;
	}

	priv->num_templates = curr + arg->size;

	for (i = priv->num_templates; i < SLOTS; i++) {
		int j = i % priv->num_templates;
		memcpy(priv->templates[i], priv->templates[j],
				priv->template_size[j]);
		priv->template_size[i] = priv->template_size[j];
	}

	return NULL;
}

static struct snobj *
command_clear(struct module *m, const char *cmd, struct snobj *arg)
{
	struct rewrite_priv *priv = get_priv(m);

	priv->next_turn = 0;
	priv->num_templates = 0;

	return NULL;
}

static const struct mclass rewrite = {
	.name 		= "Rewrite",
	.help		= "replaces entire packet data",
	.num_igates	= 1,
	.num_ogates	= 1,
	.priv_size	= sizeof(struct rewrite_priv),
	.init 		= rewrite_init,
	.process_batch 	= rewrite_process_batch,
	.commands	 = {
		{"add", 	command_add},
		{"clear", 	command_clear},
	}
};

ADD_MCLASS(rewrite)
