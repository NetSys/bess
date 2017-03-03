#ifndef BESS_UTILS_QUEUE_H
#define BESS_UTILS_QUEUE_H

namespace bess {
namespace utils {

// Takes Template argument T that is the type to be enqueued and dequeued.
template <typename T>
class Queue {
 public:
  virtual ~Queue(){};
  
  // Enqueue one object. Takes object to be added. Returns 0 on
  // success.
  virtual int Push(T) = 0;
  
  // Enqueue multiple objects. Takes a pointer to a table of objects, the
  // number of objects to be added. Returns the number of objects enqueued
  virtual int Push(T*, size_t) = 0;
  
  // Dequeue one object. Takes an object to set to the dequeued object. returns
  // zero on success 
  virtual int Pop(T&) = 0;
  

  // Dequeue several objects. Takes table to put objects and the number of objects
  // to be dequeued into the table returns the number of objects dequeued into the 
  // table 
  virtual int Pop(T*, size_t) = 0;
  
  // Returns the total capacity of the queue
  virtual size_t Capacity() = 0;
  
  // Returns the number of objects in the queue
  virtual size_t Size() = 0;
 
  // Returns true if queue is empty
  virtual bool Empty() = 0;
  
  // Returns true if full and false otherwise
  virtual bool Full() = 0;
};

}  // namespace utils
}  // namespace bess
#endif  // BESS_UTILS_QUEUE_H
