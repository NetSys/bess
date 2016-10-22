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

#include "debug.h"
#include "dpdk.h"
#include "master.h"
#include "worker.h"
#include "port.h"
#include "snbuf.h"
#include "test.h"

// Port this BESS instance listens on.
// Panda came up with this default number
static const int kDefaultPort = 0x02912; // 10514 in decimal

// TODO(barath): Rename these flags to something more intuitive.
DEFINE_bool(t, false, "Dump the size of internal data structures");
DEFINE_bool(g, false, "Run test suites");
DEFINE_string(i, "", "Specifies where to write the pidfile");
DEFINE_bool(f, false, "Run BESS in foreground mode (for developers)");
DEFINE_bool(k, false, "Kill existing BESS instance, if any");
DEFINE_bool(s, false, "Show TC statistics every second");
DEFINE_bool(d, false, "Run BESS in debug mode (with debug log messages)");
DEFINE_bool(a, false, "Allow multiple instances");

// Pidfile; Filename (nullptr=default; nullstr=none).
static char *pidfile = nullptr;

static bool ValidateCoreID(const char *flagname, int32_t value) {
  if (!is_cpu_present(value)) {
    LOG(ERROR) << "Invalid core ID: " << value;
    return false;
  }

  return true;
}
DEFINE_int32(c, 0, "Core ID for the default worker thread");

static bool ValidateTCPPort(const char *flagname, int32_t value) {
  if (value <= 0) {
    LOG(ERROR) << "Invalid TCP port number: " << value;
    return false;
  }

  return true;
}
DEFINE_int32(
    p, kDefaultPort,
    "Specifies the TCP port on which BESS listens for controller connections");

static bool ValidateMegabytesPerSocket(const char *flagname, int32_t value) {
  if (value <= 0) {
    LOG(ERROR) << "Invalid memory size: " << value;
    return false;
  }

  return true;
}
DEFINE_int32(m, 2048, "Specifies how many megabytes to use per socket");

/* NOTE: At this point DPDK has not been initilaized,
 *       so it cannot invoke rte_* functions yet. */
static void process_args(int argc, char *argv[]) {
  num_workers = 0;

  // Validate arguments.  We do this here to avoid the unused-variable warning
  // we'd get if
  // we did it at the top of the file with static declarations.
  google::RegisterFlagValidator(&FLAGS_c, &ValidateCoreID);
  google::RegisterFlagValidator(&FLAGS_p, &ValidateTCPPort);
  google::RegisterFlagValidator(&FLAGS_m, &ValidateMegabytesPerSocket);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // TODO(barath): Eliminate this sequence of ifs once we directly use FLAGS
  // from other
  // components in BESS.
  if (FLAGS_t) {
    dump_types();
    exit(EXIT_SUCCESS);
  }
  if (FLAGS_i.length() > 0) {
    if (pidfile) free(pidfile);
    pidfile = strdup(FLAGS_i.c_str()); /* Gets leaked */
  }

  if (FLAGS_g) {
    FLAGS_f = true;
    FLAGS_p = 0; /* disable the control channel */
  }

  if (FLAGS_f && !FLAGS_s) {
    LOG(INFO) << "TC statistics output is disabled (add -s option?)";
  }
}

/* todo: chdir */
void check_user() {
  uid_t euid;

  euid = geteuid();
  if (euid != 0) {
    LOG(FATAL) << "You need root privilege to run the BESS daemon";
  }

  /* Great power comes with great responsibility */
  umask(S_IWGRP | S_IWOTH);
}

/* ensure unique instance */
void check_pidfile() {
  char buf[1024];

  int fd;
  int ret;

  int trials = 0;

  pid_t pid;

  if (!pidfile)
    pidfile = (char *)"/var/run/bessd.pid";
  else if (strlen(pidfile) == 0)
    return;

  fd = open(pidfile, O_RDWR | O_CREAT, 0644);
  if (fd == -1) {
    PLOG(FATAL) << "open(pidfile)";
  }

again:
  ret = flock(fd, LOCK_EX | LOCK_NB);
  if (ret) {
    if (errno != EWOULDBLOCK) {
      PLOG(FATAL) << "flock(pidfile)";
    }

    /* lock is already acquired */
    ret = read(fd, buf, sizeof(buf) - 1);
    if (ret <= 0) {
      PLOG(FATAL) << "read(pidfile)";
    }

    buf[ret] = '\0';

    sscanf(buf, "%d", &pid);

    if (trials == 0) {
      LOG(INFO) << "There is another BESS daemon running (PID=" << pid << ")";
    }

    if (!FLAGS_k) {
      LOG(FATAL) << "You cannot run more than one BESS instance at a time "
                 << "(add -k option?)";
    }

    trials++;

    if (trials <= 3) {
      LOG(INFO) << "Sending SIGTERM signal...";

      ret = kill(pid, SIGTERM);
      if (ret < 0) {
        PLOG(FATAL) << "kill(pid, SIGTERM)";
      }

      usleep(trials * 100000);
      goto again;

    } else if (trials <= 5) {
      LOG(INFO) << "Sending SIGKILL signal...";

      ret = kill(pid, SIGKILL);
      if (ret < 0) {
        PLOG(FATAL) << "kill(pid, SIGKILL)";
      }

      usleep(trials * 100000);
      goto again;
    }

    LOG(FATAL) << "ERROR: Cannot kill the process";
  }

  if (trials > 0) {
    LOG(INFO) << "Old instance has been successfully terminated.";
  }

  ret = ftruncate(fd, 0);
  if (ret) {
    PLOG(FATAL) << "ftruncate(pidfile, 0)";
  }

  ret = lseek(fd, 0, SEEK_SET);
  if (ret) {
    PLOG(FATAL) << "lseek(pidfile, 0, SEEK_SET)";
  }

  pid = getpid();
  ret = sprintf(buf, "%d\n", pid);

  ret = write(fd, buf, ret);
  if (ret < 0) {
    PLOG(FATAL) << "write(pidfile, pid)";
  }

  /* keep the file descriptor open, to maintain the lock */
}

int daemon_start() {
  int pipe_fds[2];
  const int read_end = 0;
  const int write_end = 1;

  pid_t pid;
  pid_t sid;

  int ret;

  LOG(INFO) << "Launching BESS daemon in background...";

  ret = pipe(pipe_fds);
  if (ret < 0) {
    PLOG(FATAL) << "pipe()";
  }

  pid = fork();
  if (pid < 0) {
    PLOG(FATAL) << "fork()";
  } else if (pid > 0) {
    /* parent */
    uint64_t tmp;

    close(pipe_fds[write_end]);

    ret = read(pipe_fds[read_end], &tmp, sizeof(tmp));
    if (ret == sizeof(uint64_t)) {
      LOG(INFO) << "Done (PID=" << pid << ")";
      exit(EXIT_SUCCESS);
    } else {
      LOG(FATAL) << "Failed. (syslog may have details)";
    }
  } else {
    /* child */
    close(pipe_fds[read_end]);
    /* fall through */
  }

  /* Start a new session */
  sid = setsid();
  if (sid < 0) {
    PLOG(FATAL) << "setsid()";
  }

  return pipe_fds[write_end];
}

static void set_resource_limit() {
  struct rlimit limit = {.rlim_cur = 65536, .rlim_max = 262144};

  for (;;) {
    int ret = setrlimit(RLIMIT_NOFILE, &limit);
    if (ret == 0) return;

    if (errno == EPERM && limit.rlim_cur >= 1024) {
      limit.rlim_max /= 2;
      limit.rlim_cur = std::min(limit.rlim_cur, limit.rlim_max);
      continue;
    }

    LOG(WARNING) << "setrlimit() failed";
    return;
  }
}

void init_drivers() {
  for (auto &pair : PortBuilder::all_port_builders()) {
    if (!const_cast<PortBuilder &>(pair.second).InitPortClass()) {
      LOG(WARNING) << "Initializing driver (port class) "
                   << pair.second.class_name() << " failed.";
    }
  }
}

int main(int argc, char *argv[]) {
  int signal_fd = -1;

  google::InitGoogleLogging(argv[0]);

  gflags::SetUsageMessage("BESS Command Line Options:");
  process_args(argc, argv);

  check_user();

  if (FLAGS_f) {
    LOG(INFO) << "Launching BESS daemon in process mode...";
  } else {
    signal_fd = daemon_start();
  }

  check_pidfile();
  set_resource_limit();

  init_dpdk(argv[0], FLAGS_m, FLAGS_a);
  init_mempool();
  init_drivers();

  setup_master();

  /* signal the parent that all initialization has been finished */
  if (!FLAGS_f) {
    uint64_t one = 1;
    int ret = write(signal_fd, &one, sizeof(one));
    if (ret < 0) {
      PLOG(FATAL) << "write(signal_fd)";
    }
    close(signal_fd);
  }

  if (FLAGS_g) {
    run_tests();
  } else {
    run_forced_tests();
    run_master();
  }

  rte_eal_mp_wait_lcore();
  close_mempool();

  return 0;
}
