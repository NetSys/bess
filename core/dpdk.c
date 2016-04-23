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

#include "log.h"
#include "time.h"
#include "worker.h"
#include "snstore.h"
#include "dpdk.h"

static void set_lcore_bitmap(char *buf)
{
	int off = 0;
	int i;

	/* launch everything on core 0 for now */
	for (i = 0; i < MAX_WORKERS; i++)
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

	log_notice("/sys/devices/system/node/possible not available. "
			"Assuming a single-node system...\n");
	return 1;
}

static void disable_syslog()
{
	setlogmask(0x01);
}

static void enable_syslog()
{
	setlogmask(0xff);
}

/* for log messages during rte_eal_init() */
static ssize_t dpdk_log_init_writer(void *cookie, const char *data, size_t len)
{
	enable_syslog();
	_log(LOG_INFO, "%.*s", (int)len, data);
	disable_syslog();
	return len;
}

static cookie_io_functions_t dpdk_log_init_funcs = {
	.write = &dpdk_log_init_writer,
};

static ssize_t dpdk_log_writer(void *cookie, const char *data, size_t len)
{
	_log(LOG_INFO, "%.*s", (int)len, data);
	return len;
}

static cookie_io_functions_t dpdk_log_funcs = {
	.write = &dpdk_log_writer,
};

static void init_eal(char *prog_name, int mb_per_socket)
{
	int rte_argc = 0;
	char *rte_argv[16];

	char opt_master_lcore[1024];
	char opt_lcore_bitmap[1024];
	char opt_socket_mem[1024];

	int numa_count = get_numa_count();

	int ret;
	int i;

	FILE *org_stdout;

	sprintf(opt_master_lcore, "%d", RTE_MAX_LCORE - 1);

	set_lcore_bitmap(opt_lcore_bitmap);

	if (mb_per_socket <= 0)
		mb_per_socket = 2048;

	sprintf(opt_socket_mem, "%d", mb_per_socket);
	for(i = 1; i < numa_count; i++)
		sprintf(opt_socket_mem + strlen(opt_socket_mem), ",%d", mb_per_socket);

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

	/* DPDK creates duplicated outputs (stdout and syslog). 
	 * We temporarily disable syslog, then set our log handler */
	org_stdout = stdout;
	stdout = fopencookie(NULL, "w", dpdk_log_init_funcs);

	disable_syslog();
	ret = rte_eal_init(rte_argc, rte_argv);
	if (ret < 0) {
		log_crit("rte_eal_init() failed: ret = %d\n", ret);
		exit(EXIT_FAILURE);
	}

	enable_syslog();
	fclose(stdout);
	stdout = org_stdout;

	rte_openlog_stream(fopencookie(NULL, "w", dpdk_log_funcs));
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

void init_dpdk(char *prog_name, int mb_per_socket)
{
	init_eal(prog_name, mb_per_socket);

	tsc_hz = rte_get_tsc_hz();

	init_timer();
	init_snstore();

	announce_cpumask();
}
