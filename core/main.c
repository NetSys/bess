#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <rte_launch.h>

#include "debug.h"
#include "dpdk.h"
#include "master.h"
#include "worker.h"
#include "driver.h"

static int core_to_socket_id(int cpu)
{
	char line[256];
	char *tmp;
	FILE *fp;

	int ret;
	int i;

	fp = popen("cat /proc/cpuinfo | grep \"physical id\"", "r");
	assert(fp);

	for (i = 0; i < cpu; i++) {
		tmp = fgets(line, sizeof(line), fp);
		assert(tmp == line);
	}

	tmp = fgets(line, sizeof(line), fp);
	assert(tmp == line);

	sscanf(line, "physical id\t: %d", &ret);

	fclose(fp);

	return ret;
}

static struct {
	int wid_to_core[MAX_WORKERS];
	uint16_t port;
} cmdline_opts;

static void parse_core_list()
{
	char *ptr;

	ptr = strtok(optarg, ",");
	while (ptr != NULL) {
		if (num_workers >= MAX_WORKERS) {
			fprintf(stderr, "Cannot have more than %d workers\n",
					MAX_WORKERS);
			exit(EXIT_FAILURE);
		}

		cmdline_opts.wid_to_core[num_workers] = atoi(ptr);
		num_workers++;
		ptr = strtok(NULL, ",");
	}
}

static void print_usage(char *exec_name)
{
	fprintf(stderr, "Usage: %s [-t] [-c <core list>] [-p <port>]\n",
			exec_name);
	fprintf(stderr, "\n");

	fprintf(stderr, "  %-16s Dump the size of internal data structures\n",
			"-t");
	fprintf(stderr, "  %-16s Core ID for each worker (e.g., -c 0, 8)\n",
			"-c <core list>");
	fprintf(stderr, "  %-16s Specifies the TCP port on which SoftNIC listens"
			     "for controller connections\n",
			"-p <port>");

	exit(2);
}

/* NOTE: At this point DPDK has not been initilaized, 
 *       so it cannot invoke rte_* functions yet. */
static void init_config(int argc, char **argv)
{
	char c;

	num_workers = 0;

	while ((c = getopt(argc, argv, ":tc:p:")) != -1) {
		switch (c) {
		case 't':
			dump_types();
			exit(EXIT_SUCCESS);
			break;

		case 'c':
			parse_core_list();
			break;

		case 'p':
			sscanf(optarg, "%hu", &cmdline_opts.port);
			break;

		case ':':
			fprintf(stderr, "argument is required for -%c\n", 
					optopt);
			print_usage(argv[0]);
			break;

		case '?':
			fprintf(stderr, "Invalid option -%c\n", optopt);
			print_usage(argv[0]);
			break;

		default:
			assert(0);
		}
	}

	if (num_workers == 0) {
		/* By default, launch one worker on core 0 */
		cmdline_opts.wid_to_core[0] = 0;
		num_workers = 1;
	}
}

int main(int argc, char **argv)
{
	init_config(argc, argv);
	init_dpdk(argv[0]);
	init_mempool();
	init_drivers();

	for (int i = 0; i < num_workers; i++)
		launch_worker(i, cmdline_opts.wid_to_core[i]);

	run_master(cmdline_opts.port);

	/* never executed */
	rte_eal_mp_wait_lcore();
	close_mempool();

	return 0;
}
