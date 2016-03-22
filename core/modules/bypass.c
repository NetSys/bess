#include "../module.h"

/* This module simply passes packets from input gate X down to output gate X
 * (the same gate index) */

static void bypass_process_batch(struct module *m, struct pkt_batch *batch)
{
	run_choose_module(m, get_igate(), batch);
}

static const struct mclass bypass = {
	.name 		= "Bypass",
	.num_igates	= MAX_GATES,
	.num_ogates	= MAX_GATES,
	.process_batch 	= bypass_process_batch,
};

ADD_MCLASS(bypass)
