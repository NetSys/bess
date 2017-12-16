#include <iostream>
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
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

#ifndef BESS_UTILS_SYSCALLTHREAD_H
#define BESS_UTILS_SYSCALLTHREAD_H

#include <cassert>
#include <cerrno>
#include <thread>

#include <nmmintrin.h>
#include <signal.h>

namespace bess {
namespace utils {

bool CatchExitSignal();

/*!
 * At several points in bess we need to spin off threads that make
 * blocking system calls, and be able to tell these threads to
 * terminate.
 *
 * The actual code inside these threads varies a lot, but there
 * is a lot of common code required, which we abstract away here:
 *
 *  - pick a safe signal
 *  - establish a signal handler to get EINTR from syscalls
 *  - set proper signal masks for that signal in the thread
 *  - send such a signal from outside the thread
 *  - know when to exit the thread
 *
 * You can use SyscallThreadPfuncs or SyscallThreadAny somewhat like
 * std::thread, but it's a bit more like Python's thread class.
 * Instead of t = Thread(f), you override the Run() function in
 * your derived class.  Here's a realish example:
 *
 *   class SomeThread : public SyscallThreadAny {
 *     ...
 *     void Run() override { ... code goes here ... }
 *   };
 *   class X {
 *     ...
 *     SomeThread thread;
 *     ...
 *   };
 *   X var;
 *   ...
 *   var.thread.Start();
 *
 * and your SomeThread::Run() will be called in the started thread.
 *
 * In Run(), check IsExitRequested() any time your system call(s)
 * return(s) with an EINTR error, as these are requests for your
 * thread to terminate.  You can check it any other time as well.
 * (We check it once before even calling Run(), so your Run() may
 * not get called at all, in an extreme race case.)
 *
 * Optionally (for performance / avoiding more signals), you may
 * call BeginExiting() once you are irrevocably on the way to
 * returning.  This tells the knock thread (see below) that
 * its job is done, avoiding further signals.
 *
 * Once your Run() returns (the thread has exited), var.thread.Done()
 * will return True.
 *
 * You may WaitFor() the thread, or call Terminate(), at any time:
 * these are no-ops if the thread was never Start()ed.  By default,
 * Terminate() waits for termination.  You can call it with
 * SyscallThread::WaitType::kRequestOnly to ask it to send the
 * termination signal without waiting.
 *
 * Once Start()ed, the thread is not re-Start()able until
 * Terminate()d and/or WaitFor()ed, after which you may --
 * CAREFULLY (e.g., under locks if this could race) -- invoke
 * Reset() to put it back to "never started" state.
 *
 * Because many system calls are not available in a reliable-signal
 * flavor (cf. pselect/ppoll), requesting an exit normally starts a
 * "knock thread" that keeps kicking your Run() code to get it to
 * return.  Each kick will interrupt a system call, if you are in
 * one, but if you aren't, your next syscall will block until the
 * next kick.
 *
 * This part is optional: if (by using SyscallThreadPfuncs) you
 * declare that you are using the reliable signal system calls, and
 * never block in any other syscall, you can skip the knock thread.
 * In this case you should call Sigmask() to obtain the correct
 * mask to use in pselect/ppoll, and then you *must* check
 * IsExitRequested() immediately after the pselect/ppoll returns.
 *
 * Otherwise, there is a race between any IsExitRequested() test and
 * the entry to a system call.  This is why we have the knock thread:
 * it will repeatedly send the interrupt signal.  Eventually we
 * must win the race and you will get -1/EINTR and IsExitRequested()
 * will be true.
 *
 * This means that when using SyscallThreadAny, if you need to
 * make system calls in Run() that *must not* be interrupted, you
 * should call PushDefer() first.  Knock-thread signals will be
 * deferred until all pushed defers are popped.
 *
 * Note that BeginExiting() pushes a defer.
 *
 * MAYBE-to-do(torek): allow detaching (move the state variables
 * into a sub-object that is given to the thread, so that we can
 * use thread_.detach() and knock_thread_.detach()).  If we do this
 * we need new/dispose, std::unique_ptr<...>, and/or refcounts.
 */
class SyscallThread {
 public:
  // NB: order matters here.  Thread state progresses linearly
  // (except for InternalReset() which must not be allowed to race).
  // To allow a future Detach, we have a WaitType enumeration.
  enum class WaitType { kRequestOnly, kWait };

  // NB: the constructor is protected, as users must use one
  // of the derived classes here.
  //
  // A virtual destructor is required so that each intermediate class
  // can have its own virtual destructor that calls EndSyscallThread()
  // while the intermediate class is still active (and hence the
  // EndSyscallThread code can call the appropriate virtual functions).
  //
  // (Ideally, I'd like a pure virtual destructor here to force the
  // intermediate classes to have destuctors, but that doesn't work.)
  virtual ~SyscallThread() = default;

  bool IsExitRequested() const { return exit_requested_; }
  bool Done() const { return state_ == ThreadState::kDone; }

  /*!
   * Starts the thread running.  Will call the user provided Run()
   * once it's ready (unless the thread was already asked to exit).
   *
   * Returns true on success, false (with errno set) on failure.
   */
  bool Start() {
    // Establish process-wide signal catching, if that isn't
    // yet done.  The signal handler will interrupt system calls.
    if (!CatchExitSignal()) {
      return false;
    }

    // If the thread isn't in pristine state, this is an error.
    if (state_ != ThreadState::kNotStarted) {
      errno = EINVAL;
      return false;
    }

    assert(!thread_.joinable());
    AssertNoKnockThread();

    state_ = ThreadState::kStarting;
    thread_ = std::thread(RunInThread, this, reliable_);

    return true;
  }

  /*
   * Requests that the thread, if started, terminate.  Optionally
   * (but by default) waits for the thread to terminate.
   *
   * You may call this on the thread object from any other thread
   * (but not from within the thread itself -- just return from
   * your Run function instead).
   *
   * Does nothing if the thread was never started, or is already
   * terminated.  Note, however, that Terminate() will
   * wait for the termination to complete, after an earlier
   * t.Terminate(SyscallThread::WaitType::kRequestOnly).
   */
  void Terminate(enum WaitType waittype = WaitType::kWait) {
    if (state_ == ThreadState::kNotStarted) {
      // If never started, there is nothing to terminate.
      return;
    }

    // We should signal the thread if:
    // - it's gotten past kReady (but this test would race
    //   with the thread itself, so we just assume it has), and
    // - no one else asked it to exit yet, and
    // - it's not already on its way out, or done.
    // (if someone else already asked, that someone-else also
    // kicked off any signalling required).
    bool send_signal = !exit_requested_ && state_ < ThreadState::kExiting;
    exit_requested_ = true;
    if (send_signal) {
      SendSignal();
    }
    if (waittype == WaitType::kWait) {
      WaitFor();
    }
  }

  /*!
   * Waits for the thread to finish.  Note that this may result in
   * a scheduling yield (it potentially calls join()).  This does
   * not request termination -- it just waits, possibly forever.
   */
  void WaitFor() {
    if (thread_.joinable()) {
      thread_.join();
    }
    WaitForKnockThread();
  }

 protected:
  enum class ThreadState { kNotStarted, kStarting, kReady, kExiting, kDone };

  SyscallThread(bool reliable)
      : state_(ThreadState::kNotStarted),
        exit_requested_(false),
        reliable_(reliable),
        thread_() {}

  /*
   * Note that this destructor currently waits for the thread to
   * finish, if one was started.  If we implement detaching, this
   * should probably detach instead.
   */
  void EndSyscallThread() { Terminate(WaitType::kWait); }

  // Overridden in the final class.
  virtual void Run() = 0;

  // Overridden in the intermediate class.
  virtual void SendSignal() = 0;
  virtual void AssertNoKnockThread() = 0;
  virtual void WaitForKnockThread() = 0;

  // Internal reset: does the minimum for derived SyscallThread
  // classes.
  //
  // (If we ever allow detaching threads, Reset+InternalReset will
  // be responsible for allocating new state objects if needed.)
  bool InternalReset() {
    if (state_ == ThreadState::kNotStarted) {
      // Nothing to do.
      return true;
    }

    if (state_ != ThreadState::kDone) {
      // Inappropriate call.
      return false;
    }

    // Collect previous threads, if we have not yet.
    WaitFor();

    // Rewind state.
    exit_requested_ = false;
    state_ = ThreadState::kNotStarted;
    return true;
  }

  // Marks thread as exiting.
  void InternalBeginExiting() { state_ = ThreadState::kExiting; }

  // Detect whether thread is marked "exiting" or "exited".
  bool ExitingOrExited() const { return state_ >= ThreadState::kExiting; }

  // Kick (deliver signal to) thread to get an in-progress system
  // call to return EINTR.  Note that this just sends one signal!
  void KickThread();

 private:
  // Call the derived Run() in a thread.  Note: this function
  // is called via std::thread, so it takes an explicit pointer
  // to the instance.
  static void RunInThread(SyscallThread *thread, bool reliable);

  volatile enum ThreadState state_;
  volatile bool exit_requested_;
  bool reliable_;
  std::thread thread_;
};

// Intermediate class where the user's Run() calls pselect/ppoll.
class SyscallThreadPfuncs : public SyscallThread {
 public:
  SyscallThreadPfuncs() : SyscallThread(true) {}
  ~SyscallThreadPfuncs() { EndSyscallThread(); }

  /*!
   * Do whatever you need done asynchronously here.  Call
   * IsExitRequested() after making blocking system calls
   * (and optonally elsewhere too).
   */
  virtual void Run() override = 0;

  /*!
   * Re-sets state to allow re-firing thread.  USE WITH CAUTION!
   * Returns false if you called it inappropriately, true if it
   * did the reset.
   */
  bool Reset() { return InternalReset(); }

  /*
   * Indicates that we're on our way out of Run().
   */
  void BeginExiting() { InternalBeginExiting(); }

  /*!
   * Get the mask to pass as the sigmask argument to pselect/ppoll.
   */
  const sigset_t *Sigmask() const;

 private:
  // Provide implementations for SendSignal and knock thread.
  virtual void SendSignal() override { KickThread(); }
  virtual void AssertNoKnockThread() override {}
  virtual void WaitForKnockThread() override {}
};

// Intermediate class where the user's Run() calls any old system call(s).
class SyscallThreadAny : public SyscallThread {
 public:
  SyscallThreadAny() : SyscallThread(false), knock_thread_(), defer_count_(0) {}
  ~SyscallThreadAny() { EndSyscallThread(); }

  /*!
   * Do whatever you need done asynchronously here.  Call
   * IsExitRequested() after making blocking system calls
   * (and optonally elsewhere too).
   */
  virtual void Run() override = 0;

  /*!
   * Re-sets state to allow re-firing thread.  USE WITH CAUTION!
   * Returns false if you called it inappropriately, true if it
   * did the reset.
   */
  bool Reset() {
    bool ret = InternalReset();
    if (ret == true) {
      defer_count_ = 0;
    }
    return ret;
  }

  /*
   * Defers/disables SIG_THREAD_EXIT for any code path that
   * needs to make sure system calls *aren't* interrupted.
   */
  void PushDefer() {
    if (++defer_count_ == 1) {
      BlockSignals(true);
    }
  }

  /*
   * Re-enables and takes any pending SIG_THREAD_EXIT after
   * pushing a defer.  If you are on your way to exiting
   * there's no need to pop any pushes.
   */
  void PopDefer() {
    if (--defer_count_ == 0) {
      BlockSignals(false);
    }
  }

  /*
   * Indicates that we're on our way out of Run().
   */
  void BeginExiting() {
    PushDefer();
    InternalBeginExiting();
  }

 private:
  void BlockSignals(bool all);

  // We do have a knock thread, so these have an implementation.
  virtual void AssertNoKnockThread() override {
    assert(!knock_thread_.joinable());
  }
  virtual void WaitForKnockThread() override;

  // Sending the signal in the non-reliable-signals case requires
  // that we keep sending the signal until it is acknowledged.  We
  // use the separate knock thread for this.
  static void Knocker(SyscallThreadAny *);
  virtual void SendSignal() override {
    assert(!knock_thread_.joinable());
    knock_thread_ = std::thread(Knocker, this);
  }

  std::thread knock_thread_;
  int defer_count_;  // number of active calls to PushDefer()
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_SYSCALLTHREAD_H
