// Copyright (c) 2017, Joshua Stone.
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

#ifndef BESS_UTILS_CODEL_H_
#define BESS_UTILS_CODEL_H_

#include <cmath>
#include <deque>
#include <functional>

#include <glog/logging.h>

#include "time.h"
#include "queue.h"

namespace bess {
namespace utils {
// Codel(Controlled Delay Management) is an Queue controller based on this
// article http://queue.acm.org/detail.cfm?id=2209336

// It provides an active queue management to help prevent bufferbloat by dropping
// queue entries at an increasing rate if the delay in the queue is above the
// target queue delay. The equation used to calculate drop intervals is based on TCP
// throughput response to drop probability.

// template argument T is the type that is going to be enqueued/dequeued.
template <typename T>
class Codel final: public Queue<T> {
 public:
  // default delay target for codel
  static const uint64_t kDefaultTarget = 5000000;
  // default window size for codel
  static const uint64_t kDefaultWindow = 100000000;
  // default number of slots in the codel queue
  static const int kDefaultSlots = 4096;

  typedef std::pair<uint64_t, T> Wrapper;

  // Takes a drop function which is a function that should take a dropped object
  // and handle it removing the object potentially including freeing the object.
  // If there is no need to handle a dropped object, NULL can be passed instead.
  // target is the target delay in nanoseconds and the window is the buffer time u
  // in nanosecond before changing into drop state.
  Codel(void (*drop_func)(T)= NULL, size_t max_entries=0, uint64_t target = kDefaultTarget,
      uint64_t window = kDefaultWindow)
      : delay_target_(target),
        window_(window),
        time_above_target_(0),
        next_drop_time_(NanoSecondTime() + window),
        drop_count_(0),
        dropping_(0),
        max_size_(max_entries),
        queue_(),
        drop_func_(drop_func) { }

  // deconstructor that drops all objects still left in the internal queue.
  virtual ~Codel() {
    Wrapper w;
    while (!queue_.empty()) {
      Drop(queue_.front());
      queue_.pop_front();
    }
  }

  int Push(T obj) override {
    if (max_size_ != 0 && queue_.size() >= max_size_) {
      return -1;
    }
    Wrapper w(NanoSecondTime(), obj);
    queue_.push_back(w);
    return 0;
  }

  int Push(T* ptr, size_t count) override {
    size_t i = 0;
    for (; i < count; i++) {
      if (Push(ptr[i])) {
        break;
      }
    }
    return i;
  }

  // Retrieves the next entry from the queue and in the process, potentially drops
  // objects as well as changes between dropping state and not dropping state.
  int Pop(T &obj) override {
    bool drop = false;
    Wrapper w;
    int err = RingDequeue(w, drop);
    if (err != 0) {
      dropping_ = 0;
      return -2;
    }

    uint64_t now = NanoSecondTime();
    if (dropping_) {
      // if in dropping state, drop object until next drop time is greater
      // than the current time.
      err = DropDequeue(w, drop);
    } else if (drop && ((now - next_drop_time_ < window_) ||
                           (now - time_above_target_ >= window_))) {
      // if not in dropping state, determine whether to enter drop state and if
      // so, drop current object, get a new object and reset the drop counter.
      Drop(w);
      err = RingDequeue(w, drop);

      if (err == 0) {
        dropping_ = 1;
        if (now - next_drop_time_ < window_ && drop_count_ > 2) {
          drop_count_ -= 2;
        } else {
          drop_count_ = 1;
        }
        next_drop_time_ = NextDrop(now);
      }
    }

    // if there was a wrapper to dequeue, set the parameter object to the
    // wrapper's object
    if (err == 0) {
      obj = w.second;
    }
    return err;
  }

  // Retrieves the next count entries from the queue and in the process, potentially
  // drops objects as well as changes between dropping state and not dropping state.
  // Does not necessarily return count if there are count present but some are dropped.
  int Pop(T* objs, size_t count) override {
    size_t i = 0;
    T next_obj;
    for (; i < count; i++) {
      int err = Pop(next_obj);
      if (err != 0) {
        break;
      }
      objs[i] = next_obj;
    }
    return i;
  }
  // the underlying queue is deque which is a dynamically sized queue with a max size
  // determined by system limit. Therefore, the capacity is a specified value used to
  // limit the queue or if no value is specified, the queue's system limit.
  size_t Capacity() override {
    if (max_size_ != 0) {
      return max_size_;
    }
    return queue_.max_size();
  }

  bool Empty() override { return queue_.empty(); }

  bool Full() override {
    if (max_size_ != 0) {
      return max_size_ == queue_.size();
    }
    return queue_.size() == queue_.max_size();
  }

  size_t Size() override { return queue_.size(); }

  // The undelying queue is deque which is a dynamically sized queue with a max size
  // determined by system limits. Therefore, the resize method will error if the new_capacity
  // is outside of the queue's system limits or otherwise, only change the imposed limit on
  // the capacity of the queue.
  int Resize(size_t new_capacity) override {
    if (new_capacity <= Size()) {
      return -1;
    } else if (new_capacity >= queue_.max_size()) {
      return -1;
    }

    max_size_ = new_capacity;
    return 0;
  }

 private:
  // Calls the drop_func on the object if the drop function exists
  void Drop(Wrapper w) {
    if (drop_func_ != NULL) {
        drop_func_(w.second);
    }
  }

  // Takes the relative time to determine the next time to drop.
  // returns the next time to drop a object.
  uint64_t NextDrop(uint64_t cur_time) {
    return cur_time + window_ * pow(drop_count_, -.5);
  }

  // Gets the next object from the queue and determines based on current state,
  // whether set the passed drop boolean to true(to tell the calling function to
  // drop it). Takes a Wrapper to set to the next entry in the queue and a boolean
  // to set if the entry should be dropped. Returns 0 on success.
  int RingDequeue(Wrapper &w, bool &drop) {
    if (!queue_.empty()) {
      w = queue_.front();
      queue_.pop_front();
    } else {
      return -1;
    }

    uint64_t now = NanoSecondTime();
    uint64_t delay_time = now - w.first;

    // determine whether object should be dropped or to change state
    if (delay_time < delay_target_) {
      time_above_target_ = 0;
    } else {
      if (time_above_target_ == 0) {
        time_above_target_ = now + window_;
      } else if (now >= time_above_target_) {
        drop = true;
      }
    }
    return 0;
  }

  // Called while Codel is in drop state to determine whether to drop the current
  // entries and dequeue the next entry. Will continue to drop entries until the
  // next drop is greater than the current time. Takes a Wrapper which is the next
  // entry in the queue which will potentially be replaced and a boolean determing
  // if the entry should be dropped. Returns 0 on success.
  int DropDequeue(Wrapper &w, bool &drop) {
    uint64_t now = NanoSecondTime();
    if (!drop) {
      dropping_ = 0;
    } else if (now >= next_drop_time_) {
      while (now >= next_drop_time_ && dropping_) {
        Drop(w);
        drop_count_++;
        int err = RingDequeue(w, drop);

        if (err != 0 || !drop) {
          dropping_ = 0;
          return err;
        }
        next_drop_time_ = NextDrop(next_drop_time_);
      }
    }
    return 0;
  }

  // Returns the current time in microseconds.
  uint64_t NanoSecondTime() {
    return tsc_to_ns(rdtsc());
  }

  uint64_t delay_target_;  // the delay that codel will adjust for
  uint64_t window_;        // minimum time before changing state

  // the time at which codel will change state to above target(0 if below)
  uint64_t time_above_target_;
  uint64_t next_drop_time_;  // the next time codel will drop

  // the number of objects dropped while delay has been above target
  uint32_t drop_count_;
  uint8_t dropping_;       // whether in dropping state(above target for window)
  size_t max_size_;
  std::deque<Wrapper> queue_;  // queue
  void (*drop_func_)(T);  // the function to call to drop a value
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_CODEL_H_
