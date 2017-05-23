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

 private:
  struct llring* ring_;  // class's ring buffer
  size_t capacity_;      // the size of the backing ring buffer
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_LOCK_LESS_QUEUE_H_
