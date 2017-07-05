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

#ifndef BESS_UTILS_EXTENDED_PRIORITY_QUEUE_H_
#define BESS_UTILS_EXTENDED_PRIORITY_QUEUE_H_

#include <algorithm>
#include <queue>
#include <vector>

#include <glog/logging.h>

namespace bess {
namespace utils {

// Extends std::priority_queue to support decreasing the key of the top element
// directly.
template <typename T>
class extended_priority_queue : public std::priority_queue<T> {
 public:
  T &mutable_top() { return this->c.front(); }

  const std::vector<T> &container() const { return this->c; }

  // Notifies the priority queue that the key of the top element may have been
  // decreased, necessitating a reorganization of the heap structure.
  inline void decrease_key_top() {
    auto &v = this->c;
    const auto &cmp = this->comp;
    const size_t len = v.size();

    size_t node = 0;
    while (true) {
      size_t larger_child;
      size_t left = (2 * node) + 1;
      size_t right = (2 * node) + 2;

      if (left >= len) {
        break;
      }

      if (right >= len || cmp(v[right], v[left])) {
        larger_child = left;
      } else {
        larger_child = right;
      }
      if (!cmp(v[node], v[larger_child])) {
        break;
      }

      std::swap(v[node], v[larger_child]);
      node = larger_child;
    }

    // DCHECK(std::is_heap(v.begin(), v.end()));
  }

  template <class F>
  inline bool delete_single_element(F func) {
    auto &v = this->c;
    auto it = std::find_if(v.begin(), v.end(), func);
    if (it != v.end()) {
      v.erase(it);
      std::make_heap(v.begin(), v.end(), this->comp);

      return true;
    }
    return false;
  }
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_EXTENDED_PRIORITY_QUEUE_H_
