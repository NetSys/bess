#include "../module.h"

static void sink_op_process_batch(struct module *this,
				  struct pkt_batch *batch)
{
	snb_free_bulk(batch->pkts, batch->cnt);
}

static const struct mclass sink= {
	.name = "Sink",
	.process_batch	= sink_op_process_batch,
};

ADD_MCLASS(sink)
