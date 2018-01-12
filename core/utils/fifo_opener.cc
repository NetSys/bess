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

#include <cassert>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <glog/logging.h>

#include "fifo_opener.h"

namespace bess {
namespace utils {

/*
 * Internal function: sets O_NONBLOCK on a file descriptor.
 */
bool SetNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags & O_NONBLOCK) {
    return true;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

/*
 * Stores new path name and reconnect flag.
 * Fails if already opening/open.
 * This is essentially to allow creating an opener before
 * we know the path name and reconnect flag.
 */
int FifoOpener::Init(const std::string &path, bool reconnect) {
  std::lock_guard<std::mutex> guard(threadlock_);
  if (opening_ || shutting_down_ || RawFd() != kNotOpen) {
    return -EBUSY;
  }
  path_ = path;
  reconnect_ = reconnect;
  return 0;
}

/*!
 * Opens the fifo now, with a non-blocking open() call.
 * Returns 0 on success, or the reason the open failed,
 * e.g., -ENXIO if there is no reader, -EBUSY if there
 * is a thread working on it.
 *
 * On success, the FIFO is ready to use (is not kNotOpen).
 * Of course it may not stay that way, any future write might
 * fail if the reader goes away.
 */
int FifoOpener::OpenNow() {
  std::lock_guard<std::mutex> guard(threadlock_);

  if (opening_ || shutting_down_) {
    // If there's an opener thread running we just fail here.
    // We could, instead, stop it and try the open ourselves,
    // but for now...
    return -EBUSY;
  }

  int fd;
  uint32_t gen;
  std::tie(fd, gen) = GetCurrentFd();

  if (fd != kNotOpen) {
    return 0;  // It's already open -- there's nothing to do!
  }

  // Try open() now, using nonblocking open.  Keep holding the lock so
  // that no one else will try to open from a thread.
  fd = open(path_.c_str(), O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    return -errno;
  }

  // See FifoOpener::Run().  Opening with O_NONBLOCK may leave
  // O_NONBLOCK already in flags, but we'll be paranoid here.
  if (!SetNonblocking(fd) || !InitFifo(fd)) {
    int error = errno ? errno : EBADF;
    close(fd);
    return -error;
  }

  // Store new descriptor and update generation.  There's nothing
  // we're competing with (no new thread could be started and
  // MarkDead does nothing with old_fd being kNotOpen), so a
  // simple store suffices.
  wrapped_fd_.store(Wrap(fd, gen + 1));
  return 0;
}

bool FifoOpener::OpenInThread() {
  std::lock_guard<std::mutex> guard(threadlock_);
  if (shutting_down_) {
    return false;
  }
  if (!opening_) {
    StartThreadLocked();  // this sets opening_ to true
  }
  return true;
}

/*
 * Internal: starts the opener thread.  Caller holds the lock.
 */
void FifoOpener::StartThreadLocked() {
  assert(!opening_);
  if (thread_.Start()) {
    opening_ = true;
  } else {
    LOG_FIRST_N(ERROR, 1) << "error starting FIFO opener thread: "
                          << std::strerror(errno);
  }
}

void FifoOpenerThread::Run() {
  int fd;

  do {
    // Do a blocking open().
    fd = open(fifopath_, O_WRONLY);
  } while (fd < 0 && !IsExitRequested());

  // It's open, so we're irrevocably on the way out.
  BeginExiting();

  // Set up the file descriptor: it should be non-blocking.
  // It also needs additional setup, which is something our
  // owner/instantiator will provide.
  if (fd < 0) {
    fd = FifoOpener::kNotOpen;
  } else if (!SetNonblocking(fd) || !owner_->InitFifo(fd)) {
    close(fd);
    fd = FifoOpener::kNotOpen;
  }

  // We could test for exit-requested here, but there's no real
  // point - we can just hand over the descriptor.  Anyone poking
  // us to terminate has to wait for us to exit since we might
  // race anyway, so just give it over to our owner.
  owner_->SetNewFdFromThread(fd);
}

FifoOpener::~FifoOpener() {
  // Stop reconnects.  (Shouldn't be required, but feels safer.)
  reconnect_ = false;

  // If there is an opener thread, stop it now and wait for it to
  // finish.  In any case this is the end of this FifoOpener instance,
  // so with reconnect_ cleared, there will be no more opener threads.
  Shutdown();
}

/*
 * Stops any opener thread.  Once done, everything is back to the
 * way it was before an opener thread was started (or before
 * opening the FIFO directly).  If there was an open fd, it is
 * closed now.
 */
void FifoOpener::Shutdown() {
  {
    std::lock_guard<std::mutex> guard(threadlock_);
    shutting_down_ = true;
  }
  thread_.Terminate(bess::utils::SyscallThreadAny::WaitType::kWait);
  {
    std::lock_guard<std::mutex> guard(threadlock_);

    // While holding the lock (preventing other opens),
    // mark "not open" and close any open FIFO.  Reset
    // the thread so that new opens can happen if desired.
    // Then we're done with this shutdown.
    SetNewFdLocked(kNotOpen);
    thread_.Reset();
    shutting_down_ = false;
  }
}

/*
 * Replaces the stored fd with the given fd (which may
 * be kNotOpen).  Closes the replaced fd, if it was open.
 *
 * This is always called with the thread lock held, so it
 * does not race with other locked users; it only races against
 * MarkDead() calls.
 */
void FifoOpener::SetNewFdLocked(int fd) {
  int old_fd;
  uint32_t old_gen;
  uint64_t old_pair = wrapped_fd_.load();
  std::tie(old_fd, old_gen) = Unwrap(old_pair);
  while (old_fd != kNotOpen || fd != kNotOpen) {
    uint64_t new_pair = Wrap(fd, old_gen + 1);
    bool updated = wrapped_fd_.compare_exchange_weak(old_pair, new_pair,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed);
    if (updated) {
      break;
    }
    // wrapped_fd_ was not updated.  In other words, the
    // compare-and-swap failed ... so old_pair is updated
    // instead.
    std::tie(old_fd, old_gen) = Unwrap(old_pair);
  }

  // It was <old_fd, old_gen> and now it's <fd, new_gen>,
  // or maybe unchanged if both fd's are kNotOpen.
  //
  // If old_fd was *not* kNotOpen, it's our job to close it.
  if (old_fd != kNotOpen) {
    close(old_fd);
  }
}

/*
 * Updates the stored file descriptor.  Note: only the opener
 * thread uses this, and by doing so, indicates that the opener
 * thread is done.
 *
 * The new fd may be kNotOpen (this implies termination was
 * requested and happened before we got the FIFO open).
 */
void FifoOpener::SetNewFdFromThread(int fd) {
  std::lock_guard<std::mutex> guard(threadlock_);
  assert(opening_);
  SetNewFdLocked(fd);
  // Done with the opening, for good or ill.  Even if fd is
  // kNotOpen, we're not retrying any more.  Note: we must not
  // reset the opener thread here as we're called *from* the
  // thread, which is not quite done yet.
  opening_ = false;
}

/*
 * Updates the stored descriptor to set it to kNotOpen with a new
 * generation number, provided it's currently set to <fd,gen>.
 *
 * This can be called from any number of non-opener threads.  It
 * can race with other non-opener threads and with an opener thread.
 */
void FifoOpener::MarkDead(int fd, uint32_t gen) {
  // Generally a bit silly to call us with kNotOpen but we'll check
  if (fd == kNotOpen) {
    return;
  }

  uint64_t old_pair = Wrap(fd, gen);
  uint64_t new_pair = Wrap(kNotOpen, gen + 1);
  // No need for a loop here: either the value is the old pair,
  // and we will set it to <kNotOpen, gen+1>, or the value is no
  // longer the old pair, and someone beat us to updating it.
  bool updated = wrapped_fd_.compare_exchange_strong(
      old_pair, new_pair, std::memory_order_release, std::memory_order_relaxed);

  // If we won the compare and swap, and the FIFO should auto
  // reconnect, we get the privilege of starting the new opener
  // thread.  However, now we must take a lock and check for
  // a shutdown request.  If we should proceed here, reset the
  // thread to allow a new open, and start the process.
  if (updated && reconnect_) {
    // The old thread, if any, is done, so this should be fast.
    thread_.Terminate(bess::utils::SyscallThreadAny::WaitType::kWait);

    std::lock_guard<std::mutex> guard(threadlock_);
    if (!opening_ && !shutting_down_) {
      thread_.Reset();
      StartThreadLocked();
    }
  }
}

}  // namespace bess
}  // namespace utils
