#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/resource.h>

#include <glog/logging.h>
#include <gflags/gflags.h>

#include "opts.h"
#include "log.h"
#include "debug.h"
#include "dpdk.h"
#include "master.h"
#include "worker.h"
#include "port.h"
#include "snbuf.h"
#include "test.h"

/* Port this BESS instance listens on.
 * Panda came up with this default number */
#define DEFAULT_PORT 	0x02912		/* 10514 in decimal */

struct global_opts global_opts = {
	.port = DEFAULT_PORT,
};

static struct global_opts *opts = (struct global_opts *)&global_opts;

// TODO(barath): Rename these flags to something more intuitive.
DEFINE_bool(t, false, "Dump the size of internal data structures");
DEFINE_bool(g, false, "Run test suites");
DEFINE_string(i, "", "Specifies where to write the pidfile");
DEFINE_bool(f, false, "Run BESS in foreground mode (for developers)");
DEFINE_bool(k, false, "Kill existing BESS instance, if any");
DEFINE_bool(s, false, "Show TC statistics every second");
DEFINE_bool(d, false, "Run BESS in debug mode (with debug log messages)");
DEFINE_bool(a, false, "Allow multiple instances");

static bool ValidateCoreID(const char* flagname, int32_t value) {
  if (!is_cpu_present(value)) {
    LOG(ERROR) << "Invalid core ID: " << value;
    return false;
  }

  return true;
}
DEFINE_int32(c, -1, "Core ID for the default worker thread");

static bool ValidateTCPPort(const char* flagname, int32_t value) {
  if (value <= 0) {
    LOG(ERROR) << "Invalid TCP port number: " << value;
    return false;
  }

  return true;
}
DEFINE_int32(p, DEFAULT_PORT, "Specifies the TCP port on which BESS listens for controller connections");

static bool ValidateMegabytesPerSocket(const char* flagname, int32_t value) {
  if (value <= 0) {
    LOG(ERROR) << "Invalid memory size: " << value;
    return false;
  }

  return true;
}
DEFINE_int32(m, 2048, "Specifies how many megabytes to use per socket");


/* NOTE: At this point DPDK has not been initilaized, 
 *       so it cannot invoke rte_* functions yet. */
// Processes already-parsed gflags command-line arguments.
static void process_args() {
  num_workers = 0;

  // Validate arguments.  We do this here to avoid the unused-variable warning we'd get if
  // we did it at the top of the file with static declarations.
  google::RegisterFlagValidator(&FLAGS_c, &ValidateCoreID);
  google::RegisterFlagValidator(&FLAGS_p, &ValidateTCPPort);
  google::RegisterFlagValidator(&FLAGS_m, &ValidateMegabytesPerSocket);

  // TODO(barath): Eliminate this sequence of ifs once we directly use FLAGS from other
  // components in BESS.
  if (FLAGS_t) {
    dump_types();
    exit(EXIT_SUCCESS);
  }
  if (FLAGS_g) {
    opts->test_mode = 1;
  }

  if (FLAGS_c != -1) {
    opts->default_core = FLAGS_c;
  }

  if (FLAGS_p != -1) {
    opts->port = FLAGS_p;
  }

  if (FLAGS_f) {
    opts->foreground = 1;
  }

  if (FLAGS_k) {
    opts->kill_existing = 1;
  }

  if (FLAGS_s) {
    opts->print_tc_stats = 1;
  }

  if (FLAGS_d) {
    opts->debug_mode = 1;
  }

  if (FLAGS_m) {
    opts->mb_per_socket = FLAGS_m;
  }

  if (FLAGS_i.length() > 0) {
    if (opts->pidfile)
      free(opts->pidfile);
    opts->pidfile = strdup(FLAGS_i.c_str()); /* Gets leaked */
  }

  if (FLAGS_a) {
    opts->multi_instance = 1;
  }

	if (opts->test_mode) {
		opts->foreground = 1;
		opts->port = 0;		/* disable the control channel */
	}

	if (opts->foreground && !opts->print_tc_stats) {
		log_info("TC statistics output is disabled (add -s option?)\n");
  }
}

/* todo: chdir */
void check_user()
{
	uid_t euid;
	
	euid = geteuid();
	if (euid != 0) {
		log_err("ERROR: You need root privilege to run BESS daemon\n");
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

	if (!opts->pidfile)
		opts->pidfile = (char *)"/var/run/bessd.pid";
	else if (strlen(opts->pidfile) == 0)
		return;

	fd = open(opts->pidfile, O_RDWR | O_CREAT, 0644);
	if (fd == -1) {
		log_perr("open(pidfile)");
		exit(EXIT_FAILURE);
	}

again:
	ret = flock(fd, LOCK_EX | LOCK_NB);
	if (ret) {
		if (errno != EWOULDBLOCK) {
			log_perr("flock(pidfile)");
			exit(EXIT_FAILURE);
		}

		/* lock is already acquired */
		ret = read(fd, buf, sizeof(buf) - 1);
		if (ret <= 0) {
			log_perr("read(pidfile)");
			exit(EXIT_FAILURE);
		}

		buf[ret] = '\0';

		sscanf(buf, "%d", &pid);

		if (trials == 0)
			log_notice("  There is another BESS daemon " \
				"running (PID=%d).\n", pid);

		if (!opts->kill_existing) {
			log_err("ERROR: You cannot run more than" \
				" one BESS instance at a time. " \
				"(add -k option?)\n");
			exit(EXIT_FAILURE);
		}

		trials++;

		if (trials <= 3) {
			log_info("  Sending SIGTERM signal...\n");

			ret = kill(pid, SIGTERM);
			if (ret < 0) {
				log_perr("kill(pid, SIGTERM)");
				exit(EXIT_FAILURE);
			}

			usleep(trials * 100000);
			goto again;

		} else if (trials <= 5) {
			log_info("  Sending SIGKILL signal...\n");

			ret = kill(pid, SIGKILL);
			if (ret < 0) {
				log_perr("kill(pid, SIGKILL)");
				exit(EXIT_FAILURE);
			}

			usleep(trials * 100000);
			goto again;
		}

		log_err("ERROR: Cannot kill the process\n");
		exit(EXIT_FAILURE);
	}

	if (trials > 0)
		log_info("  Old instance has been successfully terminated.\n");

	ret = ftruncate(fd, 0);
	if (ret) {
		log_perr("ftruncate(pidfile, 0)");
		exit(EXIT_FAILURE);
	}

	ret = lseek(fd, 0, SEEK_SET);
	if (ret) {
		log_perr("lseek(pidfile, 0, SEEK_SET)");
		exit(EXIT_FAILURE);
	}

	pid = getpid();
	ret = sprintf(buf, "%d\n", pid);
	
	ret = write(fd, buf, ret);
	if (ret < 0) {
		log_perr("write(pidfile, pid)");
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

	log_info("Launching BESS daemon in background...\n");

	ret = pipe(pipe_fds);
	if (ret < 0) {
		log_perr("pipe()");
		exit(EXIT_FAILURE);
	}

	pid = fork();
	if (pid < 0) {
		log_perr("fork()");
		exit(EXIT_FAILURE);
	} else if (pid > 0) {
		/* parent */
		uint64_t tmp;

		close(pipe_fds[write_end]);

		ret = read(pipe_fds[read_end], &tmp, sizeof(tmp));
		if (ret == sizeof(uint64_t)) {
			log_info("Done (PID=%d).\n", pid);
			exit(EXIT_SUCCESS);
		} else {
			log_err("Failed. (syslog may have details)\n");
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
		log_perr("setsid()");
		exit(EXIT_FAILURE);
	}

	return pipe_fds[write_end];
}

static void set_resource_limit()
{
	struct rlimit limit = {.rlim_cur = 65536, .rlim_max = 262144};

	for (;;) {
		int ret = setrlimit(RLIMIT_NOFILE, &limit);
		if (ret == 0)
			return;

		if (errno == EPERM && limit.rlim_cur >= 1024) {
			limit.rlim_max /= 2;
			limit.rlim_cur = std::min(limit.rlim_cur, limit.rlim_max);
			continue;
		}

		log_err("WARNING: setrlimit() failed\n");
		return;
	}
}

int main(int argc, char **argv)
{
	int signal_fd = -1;

  gflags::ParseCommandLineFlags(&argc, &argv, true);
	process_args();

	check_user();
	
	if (opts->foreground)
		log_info("Launching BESS daemon in process mode...\n");
	else
		signal_fd = daemon_start();

	check_pidfile();
	set_resource_limit();

	start_logger();

	init_dpdk(argv[0], opts->mb_per_socket, opts->multi_instance);
	init_mempool();
	init_drivers();

	setup_master();

	/* signal the parent that all initialization has been finished */
	if (!opts->foreground) {
		uint64_t one = 1;
		int ret = write(signal_fd, &one, sizeof(one));
		if (ret < 0) {
			log_perr("write(signal_fd)");
			exit(EXIT_FAILURE);
		}
		close(signal_fd);
	}

	if (opts->test_mode) {
		run_tests();
	} else {
		run_forced_tests();
		run_master();
	}

	rte_eal_mp_wait_lcore();
	close_mempool();

	end_logger();

	return 0;
}
