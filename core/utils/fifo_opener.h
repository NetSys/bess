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

#ifndef BESS_UTILS_FIFO_OPENER_
#define BESS_UTILS_FIFO_OPENER_

#include <atomic>
#include <mutex>
#include <string>
#include <utility>

#include "syscallthread.h"

namespace bess {
namespace utils {

class FifoOpener;  // forward

/*
 * Internal helper thread that opens a fifo.
 *
 * There is at most one of these threads running at any time,
 * per named fifo.  We may succeed or fail; independently, we
 * may also have been requested to terminate.
 *
 * When we're told to terminate, we do so, but the fd gets
 * stored as usual (if we succeeded).
 *
 * SyscallThreadAny means that we may make any blocking system
 * calls we want, but we may get signals sent to us at any time
 * (other than between PushDefer and PopDefer calls).
 */

class FifoOpenerThread final : public bess::utils::SyscallThreadAny {
 public:
  FifoOpenerThread(const char *path, FifoOpener *owner)
      : fifopath_(path), owner_(owner) {}
  void Run() override;

 private:
  const char *fifopath_;
  FifoOpener *owner_;
};

/*!
 * A FIFO-opener opens a path (presumably one naming a FIFO device)
 * in the file system.  The open() can be immediate or deferred.  A
 * deferred open runs in a thread.  The thread will block until there
 * is a reader.  An immediate open() is done with O_NONBLOCK and
 * therefore immediately fails if there is no reader.
 *
 * Once the FIFO is open and set non-blocking, we'll invoke
 * InitFifo(), which needs to write any control headers down
 * the fifo and return true to indicate that it's now set up.
 *
 * Until the descriptor is open, its value is kNotOpen.
 *
 * If reconnect is true, the fifo file descriptor obtained in this
 * manner (whether via thread or immediate open) can be marked "dead"
 * at any time.  This will spin off a new thread to re-open the
 * descriptor.  In the meantime the descriptor is set back to
 * kNotOpen.
 *
 * Because re-opening path/to/fifo may produce the *same* file
 * descriptor that we had before, each file descriptor is actually
 * returned as a pair: <fd, gen> (generation ID).  Two different
 * descriptors, even if they have the same integer <fd> value,
 * will have different <gen> numbers.  Thus, to mark a descriptor
 * dead, you pass both values, and when obtaining a file descriptor
 * to pass to write(), you receive both values.  (Both are always
 * in the alphabetical <fd,gen> order).
 */
class FifoOpener {
 public:
  static constexpr int32_t kNotOpen = -1;
  FifoOpener() : FifoOpener(std::string(""), false){};
  FifoOpener(const std::string &path, bool reconnect)
      : path_(path),
        reconnect_(reconnect),
        wrapped_fd_(Wrap(kNotOpen, 0)),
        threadlock_(),
        opening_(false),
        shutting_down_(false),
        thread_(path_.c_str(), this) {}
  ~FifoOpener();

  /*!
   * Only allowed until calling one of the Open*()s.
   */
  int Init(const std::string &path, bool reconnect);

  virtual bool InitFifo(int fd) = 0;

  /*!
   * Opens the FIFO using a thread.  It's allowed (but expensive,
   * because this uses a lock) to call this if someone else is
   * already working on an open, or even if the FIFO was already
   * successfully opened.
   *
   * Once called, you may call Shutdown(), which will wait for
   * it to shut down completely.  No reconnects will occur after
   * this, until you again call OpenInThread() or OpenNow().
   *
   * Returns true if the open is now in progress, even if that's
   * from some other invocation.  If this returns false, it means
   * you called it inopportunely, during a Shutdown().
   */
  bool OpenInThread();

  /*!
   * Opens the FIFO right now, returning 0 on success or
   * a negative error number on failure.  It's not allowed
   * to do this if a thread is opening the fifo (you get -EBUSY),
   * nor if you are in Shutdown().
   */
  int OpenNow();

  /*
   * Breaks up the stored fd into (fd, generation) pair.
   * Note: the returned fd may be kNotOpen.
   */
  std::pair<int, uint32_t> GetCurrentFd() const {
    return Unwrap(wrapped_fd_.load());
  }

  bool IsValidFd(int fd) const { return fd != kNotOpen; }
  void MarkDead(int fd, uint32_t gen);
  void Shutdown();

 private:
  friend class FifoOpenerThread;

  // Internal functions for composing (wrapping) and breaking-up <fd,gen>
  static uint64_t Wrap(int fd, uint32_t gen) {
    return (static_cast<uint64_t>(gen) << 32) | static_cast<uint32_t>(fd);
  }
  std::pair<int, uint32_t> Unwrap(uint64_t all) const {
    int fd = static_cast<int>(all & 0xffffffff);
    uint32_t gen = static_cast<uint32_t>(all >> 32);
    return std::make_pair(fd, gen);
  }
  int RawFd() const {
    return static_cast<int>(wrapped_fd_.load() & 0xffffffff);
  }

  // Internal functions for dealing with descriptor
  void StartThreadLocked();
  void SetNewFdFromThread(int fd);
  void SetNewFdLocked(int fd);

  std::string path_;  // stable copy of path for thread
  bool reconnect_;    // whether we should auto-reconnect

  std::atomic<uint64_t> wrapped_fd_;  // protected descriptor

  std::mutex threadlock_;    // lock around open / thread operations
  bool opening_;             // flag: someone is trying to open()
  bool shutting_down_;       // flag: someone is in Shutdown()
  FifoOpenerThread thread_;  // controller for open() syscall thread
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_FIFO_OPENER_
