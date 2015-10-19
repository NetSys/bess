#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

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

const struct global_opts global_opts;
static struct global_opts *opts = (struct global_opts *)&global_opts;

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

		opts->wid_to_core[num_workers] = atoi(ptr);
		num_workers++;
		ptr = strtok(NULL, ",");
	}
}

static void print_usage(char *exec_name)
{
	fprintf(stderr, "Usage: %s" \
			" [-t] [-c <core list>] [-p <port>] [-f] [-k]\n\n",
			exec_name);

	fprintf(stderr, "  %-16s Dump the size of internal data structures\n",
			"-t");
	fprintf(stderr, "  %-16s Core ID for each worker (e.g., -c 0,8)\n",
			"-c <core list>");
	fprintf(stderr, "  %-16s Specifies the TCP port on which SoftNIC" \
			" listens for controller connections\n",
			"-p <port>");
	fprintf(stderr, "  %-16s Run BESS in foreground mode " \
			" (for developers)\n",
			"-f");
	fprintf(stderr, "  %-16s Kill existing BESS instance, if any\n",
			"-k");

	exit(2);
}

/* NOTE: At this point DPDK has not been initilaized, 
 *       so it cannot invoke rte_* functions yet. */
static void parse_args(int argc, char **argv)
{
	char c;

	num_workers = 0;

	while ((c = getopt(argc, argv, ":tc:p:fk")) != -1) {
		switch (c) {
		case 't':
			dump_types();
			exit(EXIT_SUCCESS);
			break;

		case 'c':
			parse_core_list();
			break;

		case 'p':
			sscanf(optarg, "%hu", &opts->port);
			break;

		case 'f':
			opts->foreground = 1;
			break;

		case 'k':
			opts->kill_existing = 1;
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
		opts->wid_to_core[0] = 0;
		num_workers = 1;
	}
}

/* todo: chdir */
void check_user()
{
	uid_t euid;
	
	euid = geteuid();
	if (euid != 0) {
		fprintf(stderr, "ERROR: You need root privilege to run" \
				" BESS daemon\n");
		exit(EXIT_FAILURE);
	}

	/* Great power comes with great responsibility */
	umask(S_IWGRP | S_IWOTH);
}

/* ensure unique instance */
void check_pidfile()
{
	char buf[1024];

	int fd;
	int ret;

	int trials = 0;

	pid_t pid;

	fd = open("/var/run/bessd.pid", O_RDWR | O_CREAT, 0644);
	if (fd == -1) {
		perror("open(\"/ver/run/bessd.pid\")");
		exit(EXIT_FAILURE);
	}

again:
	ret = flock(fd, LOCK_EX | LOCK_NB);
	if (ret) {
		if (errno != EWOULDBLOCK) {
			perror("flock(pidfile)");
			exit(EXIT_FAILURE);
		}

		/* lock is already acquired */
		ret = read(fd, buf, sizeof(buf) - 1);
		if (ret <= 0) {
			perror("read(pidfile)");
			exit(EXIT_FAILURE);
		}

		buf[ret] = '\0';

		sscanf(buf, "%d", &pid);

		if (trials == 0)
			printf("  There is another BESS daemon" \
				" running (PID=%d).\n", pid);

		if (!opts->kill_existing) {
			fprintf(stderr, "ERROR: You cannot run more than" \
				" one BESS instance at a time. " \
				"(add -k option?)\n");
			exit(EXIT_FAILURE);
		}

		trials++;

		if (trials <= 3) {
			printf("  Sending SIGTERM signal...\n");

			ret = kill(pid, SIGTERM);
			if (ret < 0) {
				perror("kill(pid, SIGTERM)");
				exit(EXIT_FAILURE);
			}

			usleep(trials * 100000);
			goto again;

		} else if (trials <= 5) {
			printf("  Sending SIGKILL signal...\n");

			ret = kill(pid, SIGKILL);
			if (ret < 0) {
				perror("kill(pid, SIGKILL)");
				exit(EXIT_FAILURE);
			}

			usleep(trials * 100000);
			goto again;
		}

		fprintf(stderr, "ERROR: Cannot kill the process\n");
		exit(EXIT_FAILURE);
	}

	if (trials > 0)
		printf("  Old instance has been successfully terminated.\n");

	ret = ftruncate(fd, 0);
	if (ret) {
		perror("ftruncate(pidfile, 0)");
		exit(EXIT_FAILURE);
	}

	ret = lseek(fd, 0, SEEK_SET);
	if (ret) {
		perror("lseek(pidfile, 0, SEEK_SET)");
		exit(EXIT_FAILURE);
	}

	pid = getpid();
	ret = sprintf(buf, "%d\n", pid);
	
	ret = write(fd, buf, ret);
	if (ret < 0) {
		perror("write(pidfile, pid)");
		exit(EXIT_FAILURE);
	}

	/* keep the file descriptor open, to maintain the lock */
}

int daemon_start()
{
	int pipe_fds[2];
	const int read_end = 0;
	const int write_end = 1;

	pid_t pid;
	pid_t sid;

	int ret;

	printf("Launching BESS daemon in background...\n");

	ret = pipe(pipe_fds);
	if (ret < 0) {
		perror("pipe()");
		exit(EXIT_FAILURE);
	}

	pid = fork();
	if (pid < 0) {
		perror("fork()");
		exit(EXIT_FAILURE);
	} else if (pid > 0) {
		/* parent */
		uint64_t tmp;

		close(pipe_fds[write_end]);

		ret = read(pipe_fds[read_end], &tmp, sizeof(tmp));
		if (ret == sizeof(uint64_t)) {
			printf("Done (PID=%d).\n", pid);
			exit(EXIT_SUCCESS);
		} else {
			fprintf(stderr, "Failed. (syslog may have details)\n");
			exit(EXIT_FAILURE);
		}
	} else {
		/* child */
		close(pipe_fds[read_end]);
		/* fall through */
	}

	/* Start a new session */
	sid = setsid();
	if (sid < 0) {
		perror("setsid()");
		exit(EXIT_FAILURE);
	}

	return pipe_fds[write_end];
}

int main(int argc, char **argv)
{
	int signal_fd = -1;

	parse_args(argc, argv);

	if (!opts->foreground)
		signal_fd = daemon_start();

	check_user();
	check_pidfile();

	if (!opts->foreground) {
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);

		setup_syslog();
	}

	init_dpdk(argv[0]);
	init_mempool();
	init_drivers();

	for (int i = 0; i < num_workers; i++)
		launch_worker(i, opts->wid_to_core[i]);

	setup_master(opts->port);

	/* signal the parent that all initialization has been finished */
	if (!opts->foreground) {
		int ret = write(signal_fd, &(uint64_t){1}, sizeof(uint64_t));
		if (ret < 0) {
			perror("write(signal_fd)");
			exit(EXIT_FAILURE);
		}
		close(signal_fd);
	}

	run_master();

	if (!opts->foreground)
		end_syslog();

	rte_eal_mp_wait_lcore();
	close_mempool();

	return 0;
}
