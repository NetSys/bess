#include "bessd.h"

#include <sys/file.h>

#include <gtest/gtest.h>

#include "opts.h"

namespace bess {
namespace bessd {

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
  // Write to the file.
  int fd = open("/tmp/tryacquirepidfilelocktest.log", O_RDWR | O_CREAT, 0644);
  pid_t pid = getpid();

  WritePidfile(fd, pid);
  close(fd);

  // Read from the file.
  fd = open("/tmp/tryacquirepidfilelocktest.log", O_RDONLY, 0644);

  bool success;
  pid_t readpid;
  std::tie(success, readpid) = ReadPidfile(fd);
  ASSERT_TRUE(success) << "Couldn't read pidfile due to error: " << errno;
  EXPECT_EQ(pid, readpid);
}

// Checks that we fail to write a pid value to a bad file.
TEST(WritePidFile, BadFile) {
  int fd = open("/dev/null", O_RDWR, 0644);

  EXPECT_DEATH(WritePidfile(fd, getpid()), "");
}

// Checks that trying to acquire a pidfile lock on a bad fd fails.
TEST(TryAcquirePidfileLock, BadFd) {
  EXPECT_DEATH(TryAcquirePidfileLock(-1), "");
}

// Checks that trying to acquire a pidfile lock on a new temporary file is fine.
TEST(TryAcquirePidfileLock, GoodFd) {
  bool lockheld;
  pid_t pid;

  int fd = open("/tmp/tryacquirepidfilelocktest.log", O_RDWR | O_CREAT, 0644);
  std::tie(lockheld, pid) = TryAcquirePidfileLock(fd);
  EXPECT_FALSE(lockheld);
}

// TODO(barath): Add a test case that tests what happens when another process is holding
// the file lock.
TEST(TryAcquirePidfileLock, AlreadyHeld) {
  // TODO
}

}  // namespace bessd
}  // namespace bess
