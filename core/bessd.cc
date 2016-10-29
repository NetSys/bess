#include "bessd.h"

#include <stdio.h>
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

#include <tuple>

#include <glog/logging.h>

#include "opts.h"
#include "port.h"

namespace bess {
namespace bessd {

void ProcessCommandLineArgs() {
  // TODO(barath): Eliminate this sequence of ifs once we directly use FLAGS
  // from other components in BESS.
  if (FLAGS_t) {
    dump_types();
    exit(EXIT_SUCCESS);
  }
  if (FLAGS_g) {
    FLAGS_f = true;
    FLAGS_p = 0; // Disable the control channel.
  }
  if (FLAGS_f && !FLAGS_s) {
    LOG(INFO) << "TC statistics output is disabled (add -s option?)";
  }
}

void CheckRunningAsRoot() {
  uid_t euid;

  euid = geteuid();
  if (euid != 0) {
    LOG(FATAL) << "You need root privilege to run the BESS daemon";
  }

  // Great power comes with great responsibility.
  umask(S_IWGRP | S_IWOTH);
}

void WritePidfile(int fd, pid_t pid) {
  if (ftruncate(fd, 0)) {
    PLOG(FATAL) << "ftruncate(pidfile, 0)";
  }
  if (lseek(fd, 0, SEEK_SET)) {
    PLOG(FATAL) << "lseek(pidfile, 0, SEEK_SET)";
  }

  char buf[1024];
  int pidlen = sprintf(buf, "%d\n", pid);
  if (write(fd, buf, pidlen) < 0) {
    PLOG(FATAL) << "write(pidfile, pid)";
  }
  fsync(fd);
}

std::tuple<bool, pid_t> ReadPidfile(int fd) {
  if (lseek(fd, 0, SEEK_SET)) {
    PLOG(FATAL) << "lseek(pidfile, 0, SEEK_SET)";
  }

  char buf[1024];
  int readlen = read(fd, buf, sizeof(buf) - 1);
  if (readlen <= 0) {
    if (readlen < 0) {
      PLOG(ERROR) << "read(pidfile=" << FLAGS_i << ")";
    } else {
      LOG(ERROR) << "read(pidfile=" << FLAGS_i << ")" << " at EOF";
    }

    return std::make_tuple(false, 0);
  }
  buf[readlen] = '\0';

  pid_t pid;
  sscanf(buf, "%d", &pid);

  return std::make_tuple(true, pid);
}

std::tuple<bool, pid_t> TryAcquirePidfileLock(int fd) {
  bool lockacquired = false;
  pid_t pid = 0;

  if (flock(fd, LOCK_EX | LOCK_NB)) {
    // Lock is already held by another process.
    if (errno != EWOULDBLOCK) {
      PLOG(FATAL) << "flock(pidfile=" << FLAGS_i << ")";
    } else {
      PLOG(INFO) << "flock";
    }

    // Try to read the pid of the other process.
    bool success = false;
    std::tie(success, pid) = ReadPidfile(fd);
    if (!success) {
      PLOG(FATAL) << "Couldn't read pidfile";
    }
  } else {
    lockacquired = true;
  }

  return std::make_tuple(lockacquired, pid);
}

void CheckUniqueInstance(const std::string &pidfile_path) {
  static const int kMaxPidfileLockTrials = 5;

  int fd = open(pidfile_path.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd == -1) {
    PLOG(FATAL) << "open(pidfile=" << FLAGS_i << ")";
  }

  bool lockacquired;
  pid_t pid;
  int trials;
  for (trials = 0; trials < kMaxPidfileLockTrials; trials++) {
    std::tie(lockacquired, pid) = TryAcquirePidfileLock(fd);
    if (lockacquired) {
      break;
    } else {
      if (!FLAGS_k) {
        LOG(FATAL) << "You cannot run more than one BESS instance at a time "
                   << "(add -k option?)";
      }

      if (trials == 0) {
        LOG(INFO) << "There is another BESS daemon running (PID=" << pid << ")";
      }

      if (trials < 3) {
        LOG(INFO) << "Sending SIGTERM signal...";
        if (kill(pid, SIGTERM) < 0) {
          PLOG(FATAL) << "kill(pid, SIGTERM)";
        }

        usleep(trials * 100000);
      } else if (trials < 5) {
        LOG(INFO) << "Sending SIGKILL signal...";
        if (kill(pid, SIGKILL) < 0) {
          PLOG(FATAL) << "kill(pid, SIGKILL)";
        }

        usleep(trials * 100000);
      } else {
        LOG(FATAL) << "ERROR: Cannot kill the process";
      }
    }
  }

  // We now have the pidfile lock.
  if (trials > 0) {
    LOG(INFO) << "Old instance has been successfully terminated.";
  }

  // Store our PID in the file.
  WritePidfile(fd, getpid());

  // Keep the file descriptor open, to maintain the lock.
}

int StartDaemon() {
  int pipe_fds[2];
  const int read_end = 0;
  const int write_end = 1;

  LOG(INFO) << "Launching BESS daemon in background...";

  if (pipe(pipe_fds) < 0) {
    PLOG(FATAL) << "pipe()";
  }

  pid_t pid = fork();
  if (pid < 0) {
    PLOG(FATAL) << "fork()";
  } else if (pid > 0) {
    // Parent process.
    close(pipe_fds[write_end]);

    uint64_t tmp;
    if (read(pipe_fds[read_end], &tmp, sizeof(tmp)) == sizeof(uint64_t)) {
      LOG(INFO) << "Done (PID=" << pid << ")";
      exit(EXIT_SUCCESS);
    } else {
      PLOG(FATAL) << "Failed. (syslog may have details)";
    }
  } else {
    // Child process.
    close(pipe_fds[read_end]);
    // Fall through.
  }

  // Start a new session.
  if (setsid() < 0) {
    PLOG(FATAL) << "setsid()";
  }

  return pipe_fds[write_end];
}

bool SetResourceLimit() {
  struct rlimit limit = {.rlim_cur = 65536, .rlim_max = 262144};

  for (;;) {
    if (setrlimit(RLIMIT_NOFILE, &limit) == 0) {
      return true;
    }

    if (errno == EPERM && limit.rlim_cur >= 1024) {
      limit.rlim_max /= 2;
      limit.rlim_cur = std::min(limit.rlim_cur, limit.rlim_max);
      continue;
    }

    LOG(WARNING) << "setrlimit() failed";
    return false;
  }
}

}  // namespace bessd
}  // namespace bess
