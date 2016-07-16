#include "../kmod/llring.h"

#include "../module.h"

#define DEFAULT_QUEUE_SIZE	1024

struct queue_priv {
	struct llring *queue;
};

static int resize(struct queue_priv *priv, int slots)
{
	struct llring *old_queue = priv->queue;
	struct llring *new_queue;

	int bytes = llring_bytes_with_slots(slots);

	int ret;

	new_queue = mem_alloc(bytes);
	if (!new_queue)
		return -ENOMEM;

	ret = llring_init(new_queue, slots, 0, 1);
	if (ret) {
		mem_free(new_queue);
		return -EINVAL;
	}

	/* migrate packets from the old queue */
	if (old_queue) {
		struct snbuf *pkt;

		while (llring_sc_dequeue(old_queue, (void **)&pkt) == 0) {
			ret = llring_sp_enqueue(new_queue, pkt);
			if (ret == -LLRING_ERR_NOBUF)
				snb_free(pkt);
		}

		mem_free(old_queue);
	}

	priv->queue = new_queue;

	return 0;
}

static struct snobj *
command_set_size(struct module *m, const char *cmd, struct snobj *arg);

static struct snobj *queue_init(struct module *m, struct snobj *arg)
{
	struct queue_priv *priv = get_priv(m);
	task_id_t tid;

	tid = register_task(m, NULL);
	if (tid == INVALID_TASK_ID)
		return snobj_err(ENOMEM, "Task creation failed");

	if (!arg || !(arg = snobj_eval(arg, "size"))) {
		int ret = resize(priv, DEFAULT_QUEUE_SIZE);
		return ret ? snobj_errno(-ret) : NULL;
	} else
		return command_set_size(m, NULL, arg);
}

static void queue_deinit(struct module *m)
{
	struct queue_priv *priv = get_priv(m);
	struct snbuf *pkt;

	while (llring_sc_dequeue(priv->queue, (void **)&pkt) == 0)
		snb_free(pkt);

	mem_free(priv->queue);
}

/* from upstream */
static void enqueue(struct module *m, struct pkt_batch *batch)
{
	struct queue_priv *priv = get_priv(m);

	int queued = llring_mp_enqueue_burst(priv->queue, (void **)batch->pkts,
			batch->cnt);

	if (queued < batch->cnt)
		snb_free_bulk(batch->pkts + queued, batch->cnt - queued);
}

/* to downstream */
static struct task_result dequeue(struct module *m, void *arg)
{
	struct queue_priv *priv = get_priv(m);

	struct pkt_batch batch;
	struct task_result ret;

	const int pkt_overhead = 24;

	uint64_t total_bytes = 0;

	int cnt = llring_sc_dequeue_burst(priv->queue, (void **)batch.pkts, 
			MAX_PKT_BURST);

	if (cnt > 0) {
		batch.cnt = cnt;
		run_next_module(m, &batch);
	}

	for (int i = 0; i < cnt; i++)
		total_bytes += snb_total_len(batch.pkts[i]);

	ret = (struct task_result) {
		.packets = cnt,
		.bits = (total_bytes + cnt * pkt_overhead) * 8,
	};

	return ret;
}

static struct snobj *
command_set_size(struct module *m, const char *cmd, struct snobj *arg)
{
	struct queue_priv *priv = get_priv(m);
	uint64_t val;
	int ret;

	if (snobj_type(arg) != TYPE_INT)
		return snobj_err(EINVAL, "argument must be an integer");

	val = snobj_uint_get(arg);

	if (val < 4 || val > 16384)
		return snobj_err(EINVAL, "must be in [4, 16384]");

	if (val & (val - 1))
		return snobj_err(EINVAL, "must be a power of 2");

	ret = resize(priv, val);
	if (ret)
		return snobj_errno(-ret);

	return NULL;
}

static const struct mclass queue = {
	.name			= "Queue",
	.help			=
		"terminates current task and enqueue packets for new task",
	.num_igates		= 1,
	.num_ogates		= 1,
	.priv_size		= sizeof(struct queue_priv),
	.init			= queue_init,
	.deinit			= queue_deinit,
	.process_batch		= enqueue,
	.run_task 		= dequeue,
	.commands	= {
		{"set_size", command_set_size},
	}
};

ADD_MCLASS(queue)
