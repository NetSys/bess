#include "../module.h"

static void sink_op_process_batch(struct module *this,
				  struct pkt_batch *batch)
{
	snb_free_bulk(batch->pkts, batch->cnt);
}

static const struct mclass sink= {
	.name = "Sink",
	.num_igates = 1,
	.num_ogates = 0,
	.process_batch	= sink_op_process_batch,
};

ADD_MCLASS(sink)
