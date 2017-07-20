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

#ifndef BESS_UTILS_LOCK_LESS_QUEUE_H_
#define BESS_UTILS_LOCK_LESS_QUEUE_H_

#include <glog/logging.h>

#include "kmod/llring.h"
#include "queue.h"

namespace bess {
namespace utils {

// A wrapper class for llring that extends the abstract class Queue. Takes a
// template argument T which is the type to be enqueued and dequeued.
template <typename T>
class LockLessQueue final : public Queue<T> {
 static_assert(std::is_pointer<T>::value, "LockLessQueue only supports pointer types");
 public:
  static const size_t kDefaultRingSize = 256;

  // Construct a new queue. Takes the size of backing ring buffer (must power of
  // two and entries available will be one less than specified. default is 256),
  // boolean where if true, queue is in single producer mode or if false, queue
  // is in multi producer mode, boolean where if true, queue is in single
  // consumer mode, or if false, queue is in multi consumer mode. default for
  // both booleans is true.
  LockLessQueue(size_t capacity = kDefaultRingSize, bool single_producer = true,
                bool single_consumer = true)
      : capacity_(capacity) {
    CHECK((capacity & (capacity - 1)) == 0);
    size_t ring_sz = llring_bytes_with_slots(capacity_);
    ring_ = reinterpret_cast<struct llring*>(
        aligned_alloc(alignof(llring), ring_sz));
    CHECK(ring_);
    llring_init(ring_, capacity_, single_producer, single_consumer);
  }

  virtual ~LockLessQueue() {
    if (ring_) {
      std::free(ring_);
    }
  }

  // error codes: -1 is Quota exceeded. The objects have been enqueued,
  // but the high water mark is exceeded. -2 is not enough room in the
  // ring to enqueue; no object is enqueued.
  int Push(T obj) override {
    return llring_enqueue(ring_, reinterpret_cast<void*>(obj));
  }

  int Push(T* objs, size_t count) override {
    if(!llring_enqueue_bulk(ring_, reinterpret_cast<void**>(objs), count)) {
      return count;
    }
    return 0;
  }

  int Pop(T &obj) override {
    return llring_dequeue(ring_, reinterpret_cast<void**>(&obj));
  }

  int Pop(T* objs, size_t count) override {
    if (!llring_dequeue_bulk(ring_, reinterpret_cast<void**>(objs), count)) {
      return count;
    }
    return 0;
  }

  // capacity will be one less than specified
  size_t Capacity() override { return capacity_; }

  size_t Size() override { return llring_count(ring_); }

  bool Empty() override { return llring_empty(ring_); }

  bool Full() override { return llring_full(ring_); }

  int Resize(size_t new_capacity) override {
    if (new_capacity <= Size() || (new_capacity & (new_capacity - 1))) {
      return -1;
    }

    int err;
    size_t ring_sz = llring_bytes_with_slots(new_capacity);
    llring* new_ring = reinterpret_cast<struct llring*>(malloc(ring_sz));
    CHECK(new_ring);
    err = llring_init(new_ring, new_capacity, ring_->common.sp_enqueue,
                      ring_->common.sc_dequeue);
    if (err != 0) {
      free(new_ring);
      return err;
    }

    void* obj;
    while (llring_dequeue(ring_, reinterpret_cast<void**>(&obj)) == 0) {
      llring_enqueue(new_ring, obj);
    }

    free(ring_);
    ring_ = new_ring;
    capacity_ = new_capacity;
    return 0;
  }

 private:
  struct llring* ring_;  // class's ring buffer
  size_t capacity_;      // the size of the backing ring buffer
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_LOCK_LESS_QUEUE_H_
