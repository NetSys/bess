#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <rte_launch.h>

#include "debug.h"
#include "dpdk.h"
#include "master.h"
#include "worker.h"
#include "driver.h"
#include "syslog.h"

static struct {
	int wid_to_core[MAX_WORKERS];
	uint16_t port;			/* TCP port for control channel */
	int daemonize;			
} cmdline_opts = {
	.daemonize = 1,
};

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
	fprintf(stderr, "  %-16s Do not daemonize BESS",
			"-w");

	exit(2);
}

/* NOTE: At this point DPDK has not been initilaized, 
 *       so it cannot invoke rte_* functions yet. */
static void parse_args(int argc, char **argv)
{
	char c;

	num_workers = 0;

	while ((c = getopt(argc, argv, ":tc:p:w")) != -1) {
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
		case 'w':
			cmdline_opts.daemonize = 0;
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

/* todo: chdir */
void check_user()
{
	uid_t euid;
	
	euid = geteuid();
	if (euid != 0) {
		fprintf(stderr, "You need root privilege to run BESS daemon\n");
		exit(EXIT_FAILURE);
	}

	/* Great power comes with great responsibility */
	umask(S_IRUSR | S_IWUSR);
}

void set_pidfile()
{
	char buf[1024];

	int fd;
	int ret;

	pid_t pid;

	fd = open("/var/run/bessd.pid", O_RDWR | O_CREAT);
	if (fd == -1) {
		perror("open(\"/ver/run/bessd.pid\")");
		exit(EXIT_FAILURE);
	}

	ret = flock(fd, LOCK_EX | LOCK_NB);
	if (ret) {
		/* locking failed */
		if (errno == EWOULDBLOCK) {
			ret = read(fd, buf, sizeof(buf) - 1);
			if (ret <= 0) {
				perror("read(pidfile)");
				exit(EXIT_FAILURE);
			}

			buf[ret] = '\0';

			sscanf(buf, "%d", &pid);
			fprintf(stderr, "ERROR: There is another BESS daemon" \
				" running (PID=%d).\n", pid);
			fprintf(stderr, "       You cannot run more than" \
				" one BESS instance at a time.\n");
			exit(EXIT_FAILURE);
		}
	} else {
		pid = getpid();
		ret = sprintf(buf, "%d\n", pid);
		write(fd, buf, ret);
	}
	
	/* keep the file descriptor open, to maintain the lock */
}

int daemon_start()
{
	pid_t pid;
	pid_t sid;

	pid = fork();
	if (pid < 0) {
		perror("fork()");
		return -1;
	}

	if (pid > 0)
		exit(EXIT_SUCCESS);

	/* Detach from the tty */
	sid = setsid();
	if (sid < 0) {
		perror("setsid()");
		return -1;
	}

	printf("BESS daemon is running in the background... (PID=%d)\n",
			getpid());

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	setup_syslog();

	return 0;
}

void daemon_close()
{
	end_syslog();
}

int main(int argc, char **argv)
{
	int ret = 0;

	parse_args(argc, argv);

	check_user();
	set_pidfile();

	if (cmdline_opts.daemonize) {
		ret = daemon_start();
		if (ret) {
			ret = EXIT_FAILURE;
			goto fail;
		}
	}

	init_dpdk(argv[0]);
	init_mempool();
	init_drivers();

	for (int i = 0; i < num_workers; i++)
		launch_worker(i, cmdline_opts.wid_to_core[i]);

	run_master(cmdline_opts.port);

	if (cmdline_opts.daemonize)
		daemon_close();

fail:
	rte_eal_mp_wait_lcore();
	close_mempool();

	return ret;
}
