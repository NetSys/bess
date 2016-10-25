#include "bessd.h"

#include <sys/file.h>

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

// Checks that FLAGS_g sets testing mode.
TEST(ProcessCommandLineArgs, TestingMode) {
  FLAGS_t = false;
  FLAGS_f = false;
  FLAGS_p = 1;

  FLAGS_g = true;
  ProcessCommandLineArgs();
  EXPECT_TRUE(FLAGS_f);
  EXPECT_EQ(0, FLAGS_p);
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

}  // namespace bessd
}  // namespace bess
