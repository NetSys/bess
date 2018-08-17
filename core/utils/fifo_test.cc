// Copyright (c) 2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "fifo_opener.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <unordered_map>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtest/gtest.h>

using namespace bess::utils;

namespace {

// short_sleep gives other processes a chance to run, and
// means that if we wait about 10 of these that's about .2
// seconds, which should be plenty.  However, we let the
// environment set it (needed under Travis, which is dog slow).
//
// In 18.04 with lowlatency kernel, 20 ms is not enough.  Tests
// shows it still fails at 50 and 60 ms, but succeeds at 70.  Let's
// use 100 ms (half the Travis setting) as a new default.
static std::chrono::milliseconds short_sleep = std::chrono::milliseconds(100);

// A real FIFO opener would write some data down the FIFO;
// we just make sure we get called.
class TestFifoOpener final : public FifoOpener {
 public:
  TestFifoOpener(const std::string &path, bool reconnect)
      : FifoOpener(path, reconnect), init_count_(0) {}
  bool InitFifo(int) {
    init_count_++;
    return true;
  }
  int init_count() { return init_count_; }

 private:
  int init_count_;
};

// Waits a short while for a process to finish up.
// We make 5 tries to collect it without a signal at all,
// then use SIGTERM, then wait another 20 ms per trip through
// the loop.
//
// Returns the status, or -1 on error.
int CollectOneProc(pid_t pid) {
  constexpr int tries = 10;
  int status;
  for (int i = 0; i <= tries;) {
    pid_t got = waitpid(pid, &status, i < tries ? WNOHANG : 0);
    if (got == pid) {
      return status;
    }
    if (got == -1) {
      if (errno == EINTR) {
        // let's go around again with the same i
        continue;
      }
      return -1;
    }
    // i==4 => second trip through loop, send SIGTERM
    // i==tries => last trip through loop, send SIGKILL
    if (i == 4 || i == tries) {
      kill(pid, i == 1 ? SIGTERM : SIGKILL);
    }
    // If still working on the SIGTERM case, give it a bit of time
    // before we try to collect exit status again.
    if (i < tries) {
      std::this_thread::sleep_for(short_sleep);
    }
    i++;
  }
  errno = ECHILD;  // close enough, perhaps
  return -1;
}

// Routine to wait a short while for opener.GetCurrentFd to
// return a valid fd.  Needed since asynchronous threads are,
// well, asynchronous.
//
// Returns true if it succeeds.
bool WaitForValid(TestFifoOpener &opener) {
  int fd;
  uint32_t gen;
  constexpr int tries = 10;
  for (int i = 0; i < tries;) {
    std::tie(fd, gen) = opener.GetCurrentFd();
    if (opener.IsValidFd(fd))
      return true;
    if (++i < tries) {
      std::this_thread::sleep_for(short_sleep);
    }
  }
  return false;
}

// Since most (all?) tests work by having a child proc open some
// fifo(s), we have general proc <-> proc command / response
// handling here.
//
// The way this is used is that you do:
//
//     PtoP channel;
//     PtoP.Init();             // (similar to SetUp)
//     if (channel.IsChild()) {
//       ... do child side work ...
//       channel.Exit();        // or, just return from the test here
//     }
//     ... do parent side work ...
//
// This uses fork(), so that if something goes wrong, we can just
// kill() the child entirely.  When the PtoP instance is destroyed,
// if the child is still running, it's killed; if the communications
// channels are open, they are closed.
//
// To clean up early, call Fini() from the parent (the child MUST
// use Exit()).  This wait()s for the child to exit.

class PtoP {
 public:
  PtoP(int id = -1) : parent_(0), child_(0), socket_fd_(-1), id_(id) {}
  ~PtoP() {
    Fini();
    if (parent_ > 0) {
      Exit();  // this does not return
    }
  }

  void Init() {
    int sv[2];
    ASSERT_EQ(
        0, socketpair(AF_LOCAL, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                      0, sv));
    pid_t pid = fork();
    if (pid == 0) {
      // We are the child, mark us as having a parent.
      // Don't need the actual parent pid but it seems cleaner.
      parent_ = getppid();

      // Close down sv[0], we send and recv on sv[1].
      close(sv[0]);
      socket_fd_ = sv[1];
    } else if (pid > 0) {
      // We are the parent and need to wait() for the child,
      // kill()ing it if necessary.
      child_ = pid;

      // Close down sv[1], we send and recv on sv[0].
      close(sv[1]);
      socket_fd_ = sv[0];
    } else {
      close(sv[0]);
      close(sv[1]);
      FAIL() << "fork: " << std::strerror(errno);
    }
  }

  void Exit(int status = 0) {
    if (parent_) {
      // we're the child - we should exit and not return
      _exit(status);
    }
    FAIL() << "error in test, parent called PtoP.Exit()";
  }

  // Note: returns the exit status of the child, or 0 if no child.
  int Fini() {
    int ret = 0;
    if (child_ > 0) {
      ret = CollectOneProc(child_);
    }
    if (socket_fd_ >= 0) {
      close(socket_fd_);
    }
    return ret;
  }

  bool IsChild() { return parent_ > 0; }

  // Return my name (if self is true) or my other's name (if self is false).
  // My name is "child" or "parent", possibly with my ID number added.
  std::string SelfOrOtherId(bool describe_self) {
    if (describe_self ? IsChild() : !IsChild()) {
      if (id_ != -1) {
        return std::string("child ") + std::to_string(id_);
      }
      return "child";
    } else {
      if (id_ != -1) {
        return std::string("parent ") + std::to_string(id_);
      }
      return "parent";
    }
  }
  std::string SelfId() { return SelfOrOtherId(true); }
  std::string OtherId() { return SelfOrOtherId(false); }

  // Sends message to our other (parent->child or child->parent).
  void Send(const std::string &message);

  // Gets message from our other.
  // don't get anything, the assert fires and the test aborts.
  void Recv(std::string *str);

  // Gets from other, and it must match what we expect.
  void Require(const std::string &expect);

 private:
  static const int kMaxString = 80;
  static const int kMaxTries = 10;
  // this should be kRetry but std::chrono::milliseconds(20) is not
  // a constexpr
  static bool SendString(int fd, const char *bytes, ssize_t len);
  static bool SendString(int fd, const std::string &str) {
    return SendString(fd, str.c_str(), str.length());
  }

  static bool RecvString(int fd, std::string *str, size_t maxlen);
  static bool RecvString(int fd, std::string *str) {
    return RecvString(fd, str, kMaxString);
  }

  // NB: when parent_ and child_ are both 0 we haven't forked
  // successfully.
  pid_t parent_;   // pid of parent, if we're the child, else 0
  pid_t child_;    // pid of child, if we're the parent, else 0
  int socket_fd_;  // comm channel to our parent-or-child
  int id_;         // id if needed, -1 by default
};

// Sends a counted string over one of the socket channels.
// channels.  The sockets are set non-blocking, so this has a
// short retry/timeout loop.  Returns true on success.
bool PtoP::SendString(int fd, const char *bytes, ssize_t len) {
  for (int tries = 0; tries < kMaxTries;) {
    ssize_t wrote = write(fd, bytes, len);
    if (wrote == len) {
      return true;
    }
    if (wrote != -1 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      break;
    }
    if (++tries < kMaxTries) {
      std::this_thread::sleep_for(short_sleep);
    }
  }
  return false;
}

// Receives a string.  Returns true if it gets one.  See SendString.
bool PtoP::RecvString(int fd, std::string *str, size_t maxlen) {
  char buf[maxlen];
  for (int tries = 0; tries < kMaxTries;) {
    ssize_t nread = read(fd, buf, sizeof buf);
    if (nread > 0) {
      *str = std::string(buf, nread);
      return true;
    }
    if (nread != -1 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      break;
    }
    if (++tries < kMaxTries) {
      std::this_thread::sleep_for(short_sleep);
    }
  }
  return false;
}

// Send message to parent or child, figuring out which way
// automatically.
void PtoP::Send(const std::string &message) {
  bool ok = SendString(socket_fd_, message);
  if (child_ > 0) {
    ASSERT_TRUE(ok) << "failed to send <" << message << "> to " << OtherId();
  } else {
    if (!ok) {
      std::cerr << "child: failed sending <" << message << "> to " << OtherId()
                << "\n";
      Exit();
    }
  }
}

// Get a message from parent or child, a la Send(), except that
// we create a std::string from it.
void PtoP::Recv(std::string *result) {
  bool ok = RecvString(socket_fd_, result);
  if (child_ > 0) {
    ASSERT_TRUE(ok) << "failed to get message from " << OtherId();
  } else {
    if (!ok) {
      std::cerr << SelfId() << ": failed getting message from parent\n";
      Exit();
    }
  }
}

// Requires that the next received string is what we expect.
void PtoP::Require(const std::string &expect) {
  std::string got;
  Recv(&got);
  if (child_ > 0) {
    ASSERT_EQ(got, expect);
  } else {
    if (got != expect) {
      std::cerr << SelfId() << ": expected <" << expect << ">, got <" << got
                << ">\n";
      Exit();
    }
  }
}

// Our special test fixture always reserves at least one path
// name for a FIFO and ignores SIGPIPE, so that we can write
// on a FIFO with no reader.
//
// We also add the ability to fork sub-processes and collect
// them, since we need to kick off readers so that we can
// act as writers.  To test multiple simultaneous FIFOs we
// allow additional fifo names: asking for name #1, #2, etc.,
// causes those FIFOs to be created.  All get cleaned up
// automatically on test tear-down.
//
// We include the ability to close arbitrary fds, since we
// create pipes to our children to communicate with them.
//
class FifoTestFixture : public ::testing::Test {
 public:
  FifoTestFixture()
      : nfifos_(0),
        fifoname_base_(std::string("/tmp/tfifo.") + std::to_string(getpid())),
        osig_(signal(SIGPIPE, SIG_IGN)) {}

  ~FifoTestFixture() { signal(SIGPIPE, osig_); }

  // Any ASSSERT_EQ has to go in SetUp(), not in the constructor.
  // For symmetry, we put the corresponding cleanup code in TearDown().
  void SetUp() {
    unlink(fifocstr());
    ASSERT_EQ(0, mkfifo(fifocstr(), 0666));
    nfifos_ = 1;
    // allow travis setup to extend the sleeps
    char *p = std::getenv("FIFO_TEST_TIMEOUT");
    if (p != nullptr) {
      short_sleep = std::chrono::milliseconds(std::stoi(p));
    }
  }
  void TearDown() {
    for (int i = nfifos_; --i >= 0;) {
      unlink(fifocstr(i));
    }
  }

  // Note: asking for, e.g., fifoname(3) makes fifos 0, 1, 2, 3 exist
  const std::string &fifoname() const { return fifoname_base_; }
  const std::string fifoname(int n) {
    for (int i = nfifos_; i <= n; i++) {
      const std::string path = IthFifo(i);
      unlink(path.c_str());
      // Ugh: cannot use ASSERT_EQ here as it only works from
      // a void function.  We'll just let the EXPECT generate a
      // message, and hope it explains further failures.
      EXPECT_EQ(0, mkfifo(path.c_str(), 0666));
      nfifos_ = i + 1;
    }
    return IthFifo(n);
  }
  const char *fifocstr() const { return fifoname_base_.c_str(); }
  // Beware, the return from fifocstr(n) is only good until the
  // next call (unlike fifocstr() with no arguments, which is good
  // until the test fixture instance is destroyed).
  const char *fifocstr(int n) {
    static std::string tmp;
    tmp = IthFifo(n);
    return tmp.c_str();
  }

 private:
  const std::string IthFifo(int i) const {
    if (i == 0) {
      return fifoname_base_;
    }
    return fifoname_base_ + "." + std::to_string(i);
  }

  int nfifos_;
  std::string fifoname_base_;
  void (*osig_)(int);
};

// Test OpenNow().
TEST_F(FifoTestFixture, DirectOpen) {
  TestFifoOpener opener(fifoname(), false);

  // Open should fail immediately without a reader.
  ASSERT_EQ(-ENXIO, opener.OpenNow());

  // Open should succeed immediately once there is a reader.
  PtoP channel;
  channel.Init();
  if (channel.IsChild()) {
    channel.Send("opening");
    int fd = open(fifocstr(), O_RDONLY);
    EXPECT_GE(fd, 0);
    channel.Exit();
  }
  // We can't really tell precisely when the reader blocks in open,
  // but he's sent us an "about to open" and we can then spin
  // just a little bit if needed.
  channel.Require("opening");
  for (int tries = 0;;) {
    int ret = opener.OpenNow();
    if (ret == 0) {
      break;
    }
    ASSERT_TRUE(++tries < 10) << "unable to open " << fifoname() << " in time";
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_EQ(1, opener.init_count());
  channel.Fini();

  // fd should be valid, but write() should fail now;
  // we should be able to call MarkDead and have fd become invalid.
  int fd;
  uint32_t gen;
  std::tie(fd, gen) = opener.GetCurrentFd();
  EXPECT_TRUE(opener.IsValidFd(fd));
  ssize_t ret = write(fd, "dummy", 5);
  EXPECT_EQ(-1, ret);
  opener.MarkDead(fd, gen);
  std::tie(fd, gen) = opener.GetCurrentFd();
  EXPECT_FALSE(opener.IsValidFd(fd));
}

// Test OpenInThread(), with one FIFO, one reader, and one
// pass.
TEST_F(FifoTestFixture, DeferredOpen) {
  TestFifoOpener opener(fifoname(), false);

  // Open-in-thread should succeed, but block until we
  // create a reader.
  opener.OpenInThread();
  int fd;
  uint32_t gen;
  std::tie(fd, gen) = opener.GetCurrentFd();
  ASSERT_EQ(fd, -1);

  PtoP channel;
  channel.Init();
  if (channel.IsChild()) {
    fd = open(fifocstr(), O_RDONLY);
    EXPECT_GE(fd, 0);
    if (fd >= 0) {
      channel.Send("opened");
    }
    channel.Exit();
  }
  channel.Require("opened");
  // As with direct open, fd should be valid.
  std::tie(fd, gen) = opener.GetCurrentFd();
  EXPECT_TRUE(opener.IsValidFd(fd));
  EXPECT_EQ(1, opener.init_count());
  channel.Fini();
  // Now write() should fail and we should be able to call
  // MarkDead and have fd become invalid.
  ssize_t ret = write(fd, "dummy", 5);
  EXPECT_EQ(-1, ret);
  opener.MarkDead(fd, gen);
  std::tie(fd, gen) = opener.GetCurrentFd();
  EXPECT_FALSE(opener.IsValidFd(fd));
}

// Test open with automatic reconnect (but still just one
// FIFO).
TEST_F(FifoTestFixture, Reconnect) {
  // spin off thread to open
  TestFifoOpener opener(fifoname(), true);
  ssize_t ret;
  int fd;
  uint32_t gen;
  char buf[512];
  static const char fifomsg[] = "message";

  opener.OpenInThread();

  PtoP channel;
  channel.Init();
  if (channel.IsChild()) {
    // Open for the first time (no instructions required yet).
    fd = open(fifocstr(), O_RDONLY);
    EXPECT_GE(fd, 0);
    if (fd < 0) {
      channel.Exit();
    }

    // Say that we're ready to read.
    channel.Send("ready-to-read");

    // Now read FIFO-message, which should be our known message.
    ret = read(fd, buf, sizeof buf);
    EXPECT_EQ(ret, sizeof fifomsg);

    // Wait for parent to tell us to close.
    channel.Require("close");
    close(fd);
    channel.Send("closed");

    // Wait for parent to tell us to proceed.
    channel.Require("proceed");

    // Open 2nd time, and tell parent we've done that.
    fd = open(fifocstr(), O_RDONLY);
    EXPECT_GE(fd, 0);
    if (fd < 0) {
      channel.Exit();
    }
    channel.Send("open#2");

    // Wait for instruction to exit, then do so.
    channel.Require("exit");
    channel.Exit();
  }

  // Get ready-to-read indicator from child.
  channel.Require("ready-to-read");

  // fd should now be valid, modulo possible short thread delay.
  // Write message.
  ASSERT_TRUE(WaitForValid(opener));
  EXPECT_EQ(1, opener.init_count());
  std::tie(fd, gen) = opener.GetCurrentFd();
  ret = write(fd, fifomsg, sizeof fifomsg);
  EXPECT_EQ(ret, sizeof fifomsg);

  // Tell child to close the FIFO, and wait for it to say it did.
  channel.Send("close");
  channel.Require("closed");

  // Now we expect our write to fail.
  ret = write(fd, "dummy", 5);
  ASSERT_EQ(-1, ret);
  opener.MarkDead(fd, gen);

  // Tell child to proceed; wait for it to say open-again.  The
  // fifo should soon become valid.
  channel.Send("proceed");
  channel.Require("open#2");
  ASSERT_TRUE(WaitForValid(opener));
  EXPECT_EQ(2, opener.init_count());

  // Tell child it may exit now.  It had to stick around long enough
  // for our thread open to succeed, but that's pretty much it.
  channel.Send("exit");
  channel.Fini();

  // Write should fail here, but even if it works, we can claim
  // "dead", which will spin off another re-opener thread.
  std::tie(fd, gen) = opener.GetCurrentFd();
  opener.MarkDead(fd, gen);
  std::tie(fd, gen) = opener.GetCurrentFd();
  EXPECT_FALSE(opener.IsValidFd(fd));

  // We can now terminate the re-re-opener thread cleanly.

  opener.Shutdown();
  std::tie(fd, gen) = opener.GetCurrentFd();
  EXPECT_FALSE(opener.IsValidFd(fd));
}

// Test open with automatic reconnects, with multiple different
// FIFO instances.  We should be able to shut one down without
// affecting the others.
TEST_F(FifoTestFixture, MultipleFancyFifos) {
  TestFifoOpener opener0(fifoname(), true);
  TestFifoOpener opener1(fifoname(1), true);
  TestFifoOpener opener2(fifoname(2), true);
  TestFifoOpener opener3(fifoname(3), true);
  int fd;
  uint32_t gen;

  // Spin off 4 opener threads...
  opener0.OpenInThread();
  opener1.OpenInThread();
  opener2.OpenInThread();
  opener3.OpenInThread();

  // ... and 2 children, which will open and close fifos 1 and 2.
  PtoP channel1(1);
  PtoP channel2(2);
  channel1.Init();
  if (channel1.IsChild()) {
    channel1.Require("open");
    fd = open(fifocstr(1), O_RDONLY);
    channel1.Require("close");
    close(fd);
    channel1.Exit();
  }
  // But in between, kill off opener 0, and make sure it's gone.
  std::tie(fd, gen) = opener0.GetCurrentFd();
  ASSERT_FALSE(opener0.IsValidFd(fd));
  opener0.Shutdown();
  channel2.Init();
  if (channel2.IsChild()) {
    channel2.Require("open");
    fd = open(fifocstr(2), O_RDONLY);
    channel2.Require("close");
    close(fd);
    channel2.Exit();
  }
  // Now opening fifo 0 should block forever, so when we Fini the
  // channel it should have to kill the process (takes about .2 sec).
  PtoP channel0;
  channel0.Init();
  if (channel0.IsChild()) {
    fd = open(fifocstr(), O_RDONLY);
    close(fd);
    channel0.Exit();
  }
  // Direct 1 and 2 into open; they should remain good here.
  channel1.Send("open");
  channel2.Send("open");
  // Begin killing channel 0; wait for it to end.  1 and 2 are
  // stuck in their opens.
  int status = channel0.Fini();
  ASSERT_TRUE(WIFSIGNALED(status));
  // Shut down opener 3.  This should not affect openers 1 and 2.
  opener3.Shutdown();
  ASSERT_TRUE(WaitForValid(opener1));
  ASSERT_TRUE(WaitForValid(opener2));
  channel1.Send("close");
  channel2.Send("close");
  // No need to be fancy, we can just let the destructors shut
  // everything down now.
}

}  // namespace (unnamed)
