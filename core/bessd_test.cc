#include "bessd.h"

#include <signal.h>
#include <sys/file.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "common.h"
#include "opts.h"

namespace bess {
namespace bessd {

static const char *kTestLockFilePath = "/tmp/tryacquirepidfilelocktest.log";

// Checks that FLAGS_t causes types to dump.
TEST(ProcessCommandLineArgs, DumpTypes) {
  FLAGS_t = true;
  EXPECT_EXIT(ProcessCommandLineArgs(), ::testing::ExitedWithCode(EXIT_SUCCESS), "");
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
  // Acquire the lock in a forked process.
  pid_t childpid = fork();
  if (!childpid) {
    // Child process.
    pid_t pid = getpid();

    // Write to the file.
    {
      unique_fd fd(open(kTestLockFilePath, O_RDWR | O_CREAT, 0644));

      WritePidfile(fd.get(), pid);
    }

    int fd = open(kTestLockFilePath, O_RDWR | O_CREAT, 0644);
    ASSERT_EQ(0, flock(fd, LOCK_EX | LOCK_NB)) << "Couldn't acquire file lock for test.";

    // Sleep for up to 10 seconds, while holding lock.
    sleep(10);
  } else {
    // Parent process.
    // Wait for child process to get the lock.
    sleep(2);

    unique_fd fd(open(kTestLockFilePath, O_RDWR | O_CREAT, 0644));

    bool lockacquired;
    pid_t pid;
    std::tie(lockacquired, pid) = TryAcquirePidfileLock(fd.get());

    EXPECT_FALSE(lockacquired);
    EXPECT_EQ(childpid, pid);

    kill(childpid, SIGKILL);

    unlink(kTestLockFilePath);
  }
}

// Checks that file locking dies when another process is holding the lock but
// we're not able to read the pid.
TEST(TryAcquirePidfileLock, AlreadyHeldPidReadFails) {
  // Acquire the lock in a forked process.
  pid_t childpid = fork();
  if (!childpid) {
    // Child process.
    int fd = open(kTestLockFilePath, O_RDWR | O_CREAT, 0644);
    ASSERT_EQ(0, flock(fd, LOCK_EX | LOCK_NB)) << "Couldn't acquire file lock for test.";

    // Sleep for up to 10 seconds, while holding lock.
    sleep(10);
  } else {
    // Parent process.
    // Wait for child process to get the lock.
    sleep(2);

    unique_fd fd(open(kTestLockFilePath, O_RDWR | O_CREAT, 0644));

    bool lockacquired;
    pid_t pid;
    EXPECT_DEATH(std::tie(lockacquired, pid) = TryAcquirePidfileLock(fd.get()), "");

    kill(childpid, SIGKILL);

    unlink(kTestLockFilePath);
  }
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
  // Acquire the lock in a forked process.
  pid_t childpid = fork();
  if (!childpid) {
    // Child process.
    CheckUniqueInstance(kTestLockFilePath);

    sleep(10);
  } else {
    // Parent process.
    // Wait for child process to get the lock.
    sleep(2);

    EXPECT_DEATH(CheckUniqueInstance(kTestLockFilePath), "");

    kill(childpid, SIGKILL);

    unlink(kTestLockFilePath);
  }
}

// Checks that the combined routine to check for a unique instance attempts to
// kill the child process if -k is set.
TEST(CheckUniqueInstance, HeldKillCurrentHolder) {
  // Set the command-line arg for -k.
  FLAGS_k = true;

  // Acquire the lock in a forked process.
  pid_t childpid = fork();
  if (!childpid) {
    // Ignore SIGTERM, to force parent to kill us.
    signal(SIGTERM, SIG_IGN);

    // Child process.
    CheckUniqueInstance(kTestLockFilePath);

    sleep(50);
  } else {
    // Parent process.
    // Wait for child process to get the lock.
    sleep(2);

    ASSERT_NO_FATAL_FAILURE(CheckUniqueInstance(kTestLockFilePath));

    unlink(kTestLockFilePath);
  }
}

// Checks that we can do a basic call to start the daemon.
TEST(StartDaemon, BasicRun) {
  int signal_fd = -1;
  ASSERT_NO_FATAL_FAILURE(signal_fd = StartDaemon());
  ASSERT_NE(-1, signal_fd);
  
  uint64_t one = 1;
  ASSERT_LT(-1, write(signal_fd, &one, sizeof(one)) < 0) <<
      "Couldn't write out to fd that communicates with child (daemon) process";
  close(signal_fd);
}

// Checks that we can do a basic call to set resource limits.
TEST(SetResourceLimit, BasicRun) {
  EXPECT_TRUE(SetResourceLimit());
}

}  // namespace bessd
}  // namespace bess
