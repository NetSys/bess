// Copyright (c) 2014-2016, The Regents of the University of California.
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

#ifndef BESS_UTILS_MCSLOCK_H_
#define BESS_UTILS_MCSLOCK_H_

struct mcslock_node {
  struct mcslock_node *volatile next;
  volatile char locked;
};

typedef struct mcslock_node mcslock_node_t;

struct mcslock {
  mcslock_node_t *volatile tail;
} __cacheline_aligned;

typedef struct mcslock mcslock_t;

static inline void mcs_lock_init(mcslock_t *lock) {
  lock->tail = nullptr;
}

static inline void mcs_lock(mcslock_t *lock, mcslock_node_t *mynode) {
  mcslock_node_t *pre;
  mynode->next = nullptr;
  mynode->locked = 1;

  pre = __sync_lock_test_and_set(&lock->tail, mynode);
  if (pre == nullptr) {
    return;
  }

  /* it's hold by others. queue up and spin on the node of myself */
  pre->next = mynode;

  asm volatile("sfence" ::: "memory");

  while (mynode->locked) {
    __builtin_ia32_pause();
  }
}

static inline void mcs_unlock(mcslock_t *lock, mcslock_node_t *mynode) {
  if (mynode->next == nullptr) {
    if (__sync_bool_compare_and_swap(&lock->tail, mynode, nullptr)) return;

    while (mynode->next == nullptr) {
      asm volatile("lfence" ::: "memory");
      __builtin_ia32_pause();
    }
  }

  mynode->next->locked = 0;
  return;
}

static inline int mcs_trylock(mcslock_t *lock, mcslock_node_t *mynode) {
  return __sync_bool_compare_and_swap(&lock->tail, nullptr, mynode);
}

static inline int mcs_is_locked(mcslock_t *lock) {
  return lock->tail != nullptr;
}

#endif  // BESS_UTILS_MCSLOCK_H_
