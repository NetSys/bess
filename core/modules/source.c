#include "../module.h"

#define TEMPLATE_SLOTS		(MAX_PKT_BURST * 2 - 1)
#define MAX_TEMPLATE_SIZE	1536

struct source_priv {
	int pkt_size;	/* default packet size if num_templates == 0 */

	int next_turn;	/* [0, num_templates - 1] for fair round robin */
	int num_templates;
	uint16_t template_size[TEMPLATE_SLOTS];
	unsigned char templates[TEMPLATE_SLOTS][MAX_TEMPLATE_SIZE] __ymm_aligned;
};

static struct snobj *source_query(struct module *, struct snobj *);

static struct snobj *source_init(struct module *m, struct snobj *arg)
{
	struct source_priv *priv = get_priv(m);

	priv->pkt_size = 60;	/* min-sized Ethernet frames */

	task_create(m, NULL);

	if (arg)
		return source_query(m, arg);
	
	return NULL;
}

static struct snobj *source_query(struct module *m, struct snobj *q)
{
	struct source_priv *priv = get_priv(m);

	struct snobj *templates = snobj_eval(q, "templates");

	if (templates) {
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

		for (i = templates->size; i < TEMPLATE_SLOTS; i++) {
			int j = i % templates->size;
			memcpy(priv->templates[i], priv->templates[j], 
					priv->template_size[j]);
			priv->template_size[i] = priv->template_size[j];
		}

		/* apply this at last */
		priv->num_templates = templates->size;
	}

	return NULL;
}

static struct task_result
source_run_task(struct module *m, void *arg)
{
	struct source_priv *priv = get_priv(m);

	struct pkt_batch batch;
	struct task_result ret;

	const int pkt_overhead = 24;
	const int num_templates = priv->num_templates;

	unsigned long total_bytes = 0;

	const int cnt = snb_alloc_bulk(SNBUF_PFRAME, batch.pkts, MAX_PKT_BURST, 
			priv->pkt_size);
	batch.cnt = cnt;

	if (num_templates > 0) {
		int start = priv->next_turn;
		int i;

		for (i = 0; i < cnt; i++) {
			struct snbuf *snb = batch.pkts[i];
			char *ptr = snb_head_data(snb);
			uint16_t size = priv->template_size[start + i];

			snb->mbuf.pkt_len = snb->mbuf.data_len = size;
			rte_memcpy(ptr, priv->templates[start + i], size);
			total_bytes += size;
		}

		priv->next_turn = (start + cnt) % priv->num_templates;
	}

	run_next_module(m, &batch);

	ret = (struct task_result) {
		.packets = cnt,
		.bits = (total_bytes + cnt * pkt_overhead) * 8,
	};

	return ret;
}

static const struct mclass source = {
	.name 		= "Source",
	.priv_size	= sizeof(struct source_priv),
	.init 		= source_init,
	.query		= source_query,
	.run_task 	= source_run_task,
};

ADD_MCLASS(source)
