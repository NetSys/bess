#include "../module.h"

static void merge_process_batch(struct module *m,
				struct pkt_batch *batch)
{
	run_next_module(m, batch);
}

static const struct mclass merge = {
	.name 			= "Merge",
	.help			= "All input gates go out of a single output gate",
	.num_igates		= MAX_GATES,
	.num_ogates		= 1,
	.process_batch 		= merge_process_batch,
};

ADD_MCLASS(merge)
