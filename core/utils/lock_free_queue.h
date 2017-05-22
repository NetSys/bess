#ifndef BESS_UTILS_LOCK_FREE_QUEUE_H_
#define BESS_UTILS_LOCK_FREE_QUEUE_H_

#include <glog/logging.h>

#include "../kmod/llring.h"
#include "common.h"

namespace bess {
namespace utils {

static const size_t kDefaultRingSize = 256;

template <typename T>
class LockFreeQueue {
 public:
  /**
   * Construct a new queue.
   *
   * @param capacity
   *  The size of backing ring buffer. (default: 256)
   * @param single_producer
   *  Create queue in single producer mode if true, else create queue in multi
   *  producer mode. (default: true)
   * @param single_consumer
   *  Create queue in single consumer mode if true, else create queue in multi
   *  consumer mode. (default: true)
   */
  LockFreeQueue(size_t capacity = kDefaultRingSize, bool single_producer = true,
                bool single_consumer = true)
      : capacity_(capacity) {
    size_t ring_sz = llring_bytes_with_slots(capacity_);
    ring_ = reinterpret_cast<struct llring *>(malloc(ring_sz));
    CHECK(ring_);
    llring_init(ring_, capacity_, single_producer, single_consumer);
  }

  ~LockFreeQueue() {
    if (ring_) {
      free(ring_);
    }
  }

  /**
   * Enqueue one object (NOT multi-producers safe).
   *
   * @param r
   *   A pointer to the ring structure.
   * @param obj
   *   A pointer to the object to be added.
   * @return
   *   - 0: Success; objects enqueued.
   *   - -LLRING_ERR_QUOT: Quota exceeded. The objects have been enqueued, but the
   *     high water mark is exceeded.
   *   - -LLRING_ERR_NOBUF: Not enough room in the ring to enqueue; no object is
   * enqueued.
   */
  int Push(T *obj) {
    return llring_enqueue(ring_, reinterpret_cast<void *>(obj));
  }

  /**
   * Enqueue several objects (multi-producers safe).
   *
   * This function uses a "compare and set" instruction to move the
   * producer index atomically.
   *
   * @param r
   *   A pointer to the ring structure.
   * @param obj_table
   *   A pointer to a table of void * pointers (objects).
   * @param n
   *   The number of objects to add in the ring from the obj_table.
   * @return
   *   - 0: Success; objects enqueue.
   *   - -LLRING_ERR_QUOT: Quota exceeded. The objects have been enqueued, but the
   *     high water mark is exceeded.
   *   - -LLRING_ERR_NOBUF: Not enough room in the ring to enqueue, no object is
   * enqueued.
   */
  int Push(T **objs, size_t count) {
    return llring_enqueue_bulk(ring_, reinterpret_cast<void **>(objs), count);
  }

  /**
   * Dequeue one object
   *
   * This function calls the multi-consumers or the single-consumer
   * version depending on the default behaviour that was specified at
   * ring creation time (see flags).
   *
   * @param r
   *   A pointer to the ring structure.
   * @param obj_p
   *   A pointer to a void * pointer (object) that will be filled.
   * @return
   *   - 0: Success, objects dequeued.
   *   - -LLRING_ERR_NOENT: Not enough entries in the ring to dequeue, no object
   * is
   *     dequeued.
   */
  int Pop(T **obj) {
    return llring_dequeue(ring_, reinterpret_cast<void **>(obj));
  }

  /**
   * Dequeue several objects
   *
   * This function calls the multi-consumers or the single-consumer
   * version, depending on the default behaviour that was specified at
   * ring creation time (see flags).
   *
   * @param r
   *   A pointer to the ring structure.
   * @param obj_table
   *   A pointer to a table of void * pointers (objects) that will be filled.
   * @param n
   *   The number of objects to dequeue from the ring to the obj_table.
   * @return
   *   - 0: Success; objects dequeued.
   *   - -LLRING_ERR_NOENT: Not enough entries in the ring to dequeue, no object
   * is
   *     dequeued.
   */
  int Pop(T **objs, size_t count) {
    return llring_dequeue_bulk(ring_, reinterpret_cast<void **>(objs), count);
  }

  // Returns the capacity of the queue
  size_t Capacity() { return capacity_; }

  // Returns the number of objects in the queue 
  size_t Size() {
	  uint32_t cons_head, prod_tail;
		cons_head = ring_->cons.head;
		prod_tail = ring_->prod.tail;
    return prod_tail - cons_head;
  }

  bool Empty() { return Size() == 0; }

 private:
  struct llring *ring_;
  size_t capacity_;
};

}
}

#endif  // BESS_UTILS_LOCK_FREE_QUEUE_H_
