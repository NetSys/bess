#include "bessd.h"

#include <sys/file.h>
#include <sys/select.h>
#include <unistd.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <csignal>
#include <string>

#include "opts.h"
#include "utils/common.h"

namespace bess {
namespace bessd {

static const char *kTestLockFilePath = "/tmp/tryacquirepidfilelocktest.log";

// Executes CHILD before PARENT, then once PARENT completes, signals
// the child and waits for its exit code, which we expect to be SIGNAL.
// This ended up having to be a macro because gtest DEATH tests did not work
// with lambda expressions.
#define DO_MULTI_PROCESS_TEST(CHILD, PARENT, SIGNAL)                          \
  const int kSelectTimeoutInSecs = 2;                                         \
  const char *kSignalText = "foo";                                            \
  const int kSignalTextLen = 4;                                               \
                                                                              \
  int child_to_parent[2];                                                     \
  int parent_to_child[2];                                                     \
                                                                              \
  ASSERT_EQ(0, pipe(child_to_parent)) << "pipe() failed";                     \
  ASSERT_EQ(0, pipe(parent_to_child)) << "pipe() failed";                     \
                                                                              \
  pid_t childpid = fork();                                                    \
  ASSERT_NE(-1, childpid) << "fork() failed.";                                \
  if (!childpid) {                                                            \
    CHILD;                                                                    \
                                                                              \
    ignore_result(write(child_to_parent[1], kSignalText, kSignalTextLen));    \
                                                                              \
    char buf[kSignalTextLen];                                                 \
                                                                              \
    ignore_result(read(parent_to_child[0], buf, kSignalTextLen));             \
                                                                              \
    exit(0);                                                                  \
  } else {                                                                    \
    fd_set read_fds, write_fds, err_fds;                                      \
    struct timeval tv = {kSelectTimeoutInSecs, 0};                            \
    FD_ZERO(&read_fds);                                                       \
    FD_ZERO(&err_fds);                                                        \
    FD_SET(child_to_parent[0], &read_fds);                                    \
    FD_SET(child_to_parent[0], &err_fds);                                     \
                                                                              \
    int ret =                                                                 \
        select(child_to_parent[0] + 1, &read_fds, nullptr, &err_fds, &tv);    \
    ASSERT_NE(0, ret) << "select() timed out in parent.";                     \
    ASSERT_NE(-1, ret) << "select() had an error " << errno;                  \
    ASSERT_TRUE(FD_ISSET(child_to_parent[0], &read_fds))                      \
        << "Child didn't send us anything.";                                  \
    char buf[kSignalTextLen];                                                 \
    ASSERT_EQ(kSignalTextLen, read(child_to_parent[0], buf, kSignalTextLen)); \
                                                                              \
    PARENT;                                                                   \
                                                                              \
    tv = {kSelectTimeoutInSecs, 0};                                           \
    FD_ZERO(&write_fds);                                                      \
    FD_ZERO(&err_fds);                                                        \
    FD_SET(parent_to_child[1], &write_fds);                                   \
    FD_SET(parent_to_child[1], &err_fds);                                     \
    ret = select(parent_to_child[1] + 1, nullptr, &write_fds, &err_fds, &tv); \
    ASSERT_NE(0, ret) << "select() timed out in parent trying to write.";     \
    ASSERT_NE(-1, ret) << "select() had an error " << errno;                  \
    ASSERT_TRUE(FD_ISSET(parent_to_child[1], &write_fds)) << "Can't write.";  \
                                                                              \
    ignore_result(write(parent_to_child[1], kSignalText, kSignalTextLen));    \
                                                                              \
    int status;                                                               \
    waitpid(childpid, &status, 0);                                            \
    if (SIGNAL) {                                                             \
      EXPECT_NE(0, status);                                                   \
      ASSERT_TRUE(WIFSIGNALED(status));                                       \
      EXPECT_EQ(SIGNAL, WTERMSIG(status));                                    \
    } else {                                                                  \
      ASSERT_EQ(0, status);                                                   \
    }                                                                         \
  }

// Checks that FLAGS_t causes types to dump.
TEST(ProcessCommandLineArgs, DumpTypes) {
  FLAGS_t = true;
  EXPECT_EXIT(ProcessCommandLineArgs(), ::testing::ExitedWithCode(EXIT_SUCCESS),
              "");
}

// Checks that running as non-root causes termination.
TEST(CheckRunningAsRoot, NonRoot) {
  // Only do the test if we're not root.
  if (geteuid()) {
    EXPECT_DEATH(CheckRunningAsRoot(), "");
  }
}

// Checks that we can write out and read in a pid value to/from a good file.
TEST(WriteAndReadPidFile, GoodFile) {
  pid_t pid = getpid();

  // Write to the file.
  {
    unique_fd fd(open(kTestLockFilePath, O_RDWR | O_CREAT, 0644));

    WritePidfile(fd.get(), pid);
  }

  // Read from the file.
  {
    unique_fd fd(open(kTestLockFilePath, O_RDONLY));

    bool success;
    pid_t readpid;
    std::tie(success, readpid) = ReadPidfile(fd.get());
    ASSERT_TRUE(success) << "Couldn't read pidfile due to error: " << errno;
    EXPECT_EQ(pid, readpid);
  }

  unlink(kTestLockFilePath);
}

// Checks that we fail to write a pid value to a bad file.
TEST(WritePidFile, BadFile) {
  unique_fd fd(open("/dev/null", O_RDWR, 0644));

  EXPECT_DEATH(WritePidfile(fd.get(), getpid()), "");
}

// Checks that we fail to read a pid value from a bad file desciptor.
TEST(ReadPidFile, BadFileDesciptor) {
  bool success;
  pid_t readpid;
  EXPECT_DEATH(std::tie(success, readpid) = ReadPidfile(-1), "");
}

// Checks that we fail to read a pid value from a bad file.
TEST(ReadPidFile, BadFile) {
  unique_fd fd(open("/dev/null", O_RDWR, 0644));
  ASSERT_NE(-1, fd.get()) << "Couldn't open /dev/null";

  bool success;
  pid_t readpid;
  std::tie(success, readpid) = ReadPidfile(fd.get());

  EXPECT_FALSE(success);
  EXPECT_EQ(0, readpid);
}

// Checks that trying to acquire a pidfile lock on a bad fd fails.
TEST(TryAcquirePidfileLock, BadFd) {
  EXPECT_DEATH(TryAcquirePidfileLock(-1), "");
}

// Checks that trying to acquire a pidfile lock on a new temporary file is fine.
TEST(TryAcquirePidfileLock, GoodFd) {
  unique_fd fd(open(kTestLockFilePath, O_RDWR | O_CREAT, 0644));

  bool lockacquired;
  pid_t pid;
  std::tie(lockacquired, pid) = TryAcquirePidfileLock(fd.get());
  EXPECT_TRUE(lockacquired) << "Lock already held by pid " << pid;

  unlink(kTestLockFilePath);
}

// Checks that file locking fails when another process is holding the lock.
TEST(TryAcquirePidfileLock, AlreadyHeld) {
  DO_MULTI_PROCESS_TEST(
      {
        {
          pid_t pid = getpid();
          unique_fd fd(open(kTestLockFilePath, O_RDWR | O_CREAT, 0644));

          WritePidfile(fd.get(), pid);
        }

        int fd = open(kTestLockFilePath, O_RDWR | O_CREAT, 0644);
        ASSERT_EQ(0, flock(fd, LOCK_EX | LOCK_NB))
            << "Couldn't acquire file lock for test.";
      },
      {
        unique_fd fd(open(kTestLockFilePath, O_RDWR | O_CREAT, 0644));

        bool lockacquired;
        pid_t pid;
        std::tie(lockacquired, pid) = TryAcquirePidfileLock(fd.get());

        EXPECT_FALSE(lockacquired);
        EXPECT_EQ(childpid, pid);

        unlink(kTestLockFilePath);
      },
      0);
}

// Checks that file locking dies when another process is holding the lock but
// we're not able to read the pid.
TEST(TryAcquirePidfileLock, AlreadyHeldPidReadFails) {
  DO_MULTI_PROCESS_TEST(
      {
        int fd = open(kTestLockFilePath, O_RDWR | O_CREAT, 0644);
        ASSERT_EQ(0, flock(fd, LOCK_EX | LOCK_NB))
            << "Couldn't acquire file lock for test.";
      },
      {
        unique_fd fd(open(kTestLockFilePath, O_RDWR | O_CREAT, 0644));

        bool lockacquired;
        pid_t pid;
        EXPECT_DEATH(
            std::tie(lockacquired, pid) = TryAcquirePidfileLock(fd.get()), "");
        unlink(kTestLockFilePath);
      },
      0);
}

// Checks that trying to check for a unique instance with a bad pidfile path
// dies.
TEST(CheckUniqueInstance, BadPidfilePath) {
  // Assume we don't have permission to write to this path:
  const std::string kNoPermissionPath("/dev/nopermission");
  EXPECT_DEATH(CheckUniqueInstance(kNoPermissionPath), "");
}

// Checks that the combined routine to check for a unique instance works when
// the lock isn't held.
TEST(CheckUniqueInstance, NotHeld) {
  ASSERT_NO_FATAL_FAILURE(CheckUniqueInstance(kTestLockFilePath));

  // Release lock for later tests.
  int fd = open(kTestLockFilePath, O_RDWR | O_CREAT, 0644);
  flock(fd, LOCK_UN);
  close(fd);
  unlink(kTestLockFilePath);
}

// Checks that the combined routine to check for a unique instance fails
// properly when the lock is already held.
TEST(CheckUniqueInstance, Held) {
  DO_MULTI_PROCESS_TEST({ CheckUniqueInstance(kTestLockFilePath); },
                        {
                          EXPECT_DEATH(CheckUniqueInstance(kTestLockFilePath),
                                       "");
                          unlink(kTestLockFilePath);
                        },
                        0);
}

// Checks that the combined routine to check for a unique instance attempts to
// kill the child process if -k is set.
TEST(CheckUniqueInstance, HeldKillCurrentHolder) {
  // Set the command-line arg for -k.
  FLAGS_k = true;

  DO_MULTI_PROCESS_TEST(
      {
        // Ignore SIGTERM, to force parent to kill us.
        signal(SIGTERM, SIG_IGN);

        // Child process.
        int pidfile_fd = CheckUniqueInstance(kTestLockFilePath);
        WritePidfile(pidfile_fd, getpid());
      },
      {
        ASSERT_NO_FATAL_FAILURE(CheckUniqueInstance(kTestLockFilePath));
        unlink(kTestLockFilePath);
      },
      SIGKILL);
}

// Checks that we can do a basic call to start the daemon.
TEST(Daemonize, BasicRun) {
  DO_MULTI_PROCESS_TEST(
      {
        int signal_fd = -1;
        ASSERT_NO_FATAL_FAILURE(signal_fd = Daemonize(););
        ASSERT_NE(-1, signal_fd);

        uint64_t one = 1;
        ASSERT_LT(-1, write(signal_fd, &one, sizeof(one)) < 0)
            << "Couldn't write out to fd that communicates with parent process";
        close(signal_fd);
      },
      {}, 0);
}

// Checks that we can do a basic call to set resource limits.
TEST(SetResourceLimit, BasicRun) {
  EXPECT_TRUE(SetResourceLimit());
}

// Checks that we can get the executable's own directory.
TEST(GetCurrentDirectory, BasicRun) {
  EXPECT_NE(GetCurrentDirectory(), "");
}

}  // namespace bessd
}  // namespace bess
