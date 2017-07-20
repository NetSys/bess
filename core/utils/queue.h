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

  // Resizes the queue to the specified new capacity which must be larger than
  // the current size. Returns 0 on success.
  virtual int Resize(size_t) = 0;
};

}  // namespace utils
}  // namespace bess
#endif  // BESS_UTILS_QUEUE_H
