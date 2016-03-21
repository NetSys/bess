#include "../module.h"

static void bypass_process_batch(struct module *m, struct pkt_batch *batch)
{
	run_next_module(m, batch);
}

static const struct mclass bypass = {
	.name 		= "Bypass",
	.num_igates	= 1,
	.num_ogates	= 1,
	.process_batch 	= bypass_process_batch,
};

ADD_MCLASS(bypass)
