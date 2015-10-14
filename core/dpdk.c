#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <rte_config.h>
#include <rte_cycles.h>
#include <rte_timer.h>
#include <rte_ethdev.h>
#include <rte_eal.h>

#include <sn.h>

#include "dpdk.h"
#include "time.h"
#include "worker.h"
#include "snstore.h"

static void set_lcore_bitmap(char *buf)
{
	int off = 0;
	int i;

	/* launch everything on core 0 for now */
	for (i = 0; i < num_workers; i++)
		off += sprintf(buf + off, "%d@0,", i);

	off += sprintf(buf + off, "%d@%d", RTE_MAX_LCORE - 1, 0);
}

static int get_numa_count()
{
	FILE *fp;

	int matched;
	int cnt;

	fp = fopen("/sys/devices/system/node/possible", "r");
	if (!fp)
		goto fail;

	matched = fscanf(fp, "0-%d", &cnt);
	if (matched == 1)
		return cnt + 1;

fail:
	if (fp)
		fclose(fp);

	fprintf(stderr, "Failed to detect # of NUMA nodes from: "
			"/sys/devices/system/node/possible. "
			"Assuming a single-node system...\n");
	return 1;
}

static void init_eal(char *prog_name)
{
	int rte_argc = 0;
	char *rte_argv[16];

	char opt_master_lcore[1024];
	char opt_lcore_bitmap[1024];
	char opt_socket_mem[1024];

	const char *socket_mem = "1024";

	int numa_count = get_numa_count();

	int ret;
	int i;

	sprintf(opt_master_lcore, "%d", RTE_MAX_LCORE - 1);

	set_lcore_bitmap(opt_lcore_bitmap);

	sprintf(opt_socket_mem, "%s", socket_mem);
	for(i = 1; i < numa_count; i++)
		sprintf(opt_socket_mem + strlen(opt_socket_mem), ",%s", socket_mem);

	rte_argv[rte_argc++] = prog_name;
	rte_argv[rte_argc++] = "--master-lcore";
	rte_argv[rte_argc++] = opt_master_lcore;
	rte_argv[rte_argc++] = "--lcore";
	rte_argv[rte_argc++] = opt_lcore_bitmap;
	rte_argv[rte_argc++] = "-n";
	rte_argv[rte_argc++] = "4";	/* number of memory channels (Sandy Bridge) */
#if 1
	rte_argv[rte_argc++] = "--socket-mem";
	rte_argv[rte_argc++] = opt_socket_mem;
#else
	rte_argv[rte_argc++] = "--no-huge";
#endif
	rte_argv[rte_argc] = NULL;

	/* reset getopt() */
	optind = 0;

	ret = rte_eal_init(rte_argc, rte_argv);
	assert(ret >= 0);
}

static void init_timer()
{
	rte_timer_subsystem_init();
}

#if DPDK < DPDK_VER(2, 0, 0)
  #error DPDK 2.0.0 or higher is required
#endif

/* Secondary processes need to run on distinct cores */
static void announce_cpumask()
{
#if 0
	uint64_t cpumask = 0;
	int i;

	for (i = 0; i < num_workers; i++)
		cpumask |= (1 << wid_to_lcore_map[i]);

	snstore_put("_cpumask", (void *)cpumask);
#endif
}

void init_dpdk(char *prog_name)
{
	init_eal(prog_name);

	tsc_hz = rte_get_tsc_hz();

	init_timer();
	init_snstore();

	announce_cpumask();
}
