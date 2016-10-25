#ifndef _MCS_LOCK_H_
#define _MCS_LOCK_H_

#include "../common.h"

struct mcslock_node {
  struct mcslock_node *volatile next;
  volatile char locked;
};

typedef struct mcslock_node mcslock_node_t;

struct mcslock {
  mcslock_node_t *volatile tail;
} __cacheline_aligned;

typedef struct mcslock mcslock_t;

static inline void mcs_lock_init(mcslock_t *lock) { lock->tail = NULL; }

static inline void mcs_lock(mcslock_t *lock, mcslock_node_t *mynode) {
  mcslock_node_t *pre;
  mynode->next = NULL;
  mynode->locked = 1;

  pre = __sync_lock_test_and_set(&lock->tail, mynode);
  if (pre == NULL) return;

  /* it's hold by others. queue up and spin on the node of myself */
  pre->next = mynode;

  asm volatile("sfence" ::: "memory");
  while (mynode->locked) __builtin_ia32_pause();
}

static inline void mcs_unlock(mcslock_t *lock, mcslock_node_t *mynode) {
  if (mynode->next == NULL) {
    if (__sync_bool_compare_and_swap(&lock->tail, mynode, NULL)) return;

    while (mynode->next == NULL) {
      asm volatile("lfence" ::: "memory");
      __builtin_ia32_pause();
    }
  }

  mynode->next->locked = 0;
  return;
}

static inline int mcs_trylock(mcslock_t *lock, mcslock_node_t *mynode) {
  return __sync_bool_compare_and_swap(&lock->tail, NULL, mynode);
}

static inline int mcs_is_locked(mcslock_t *lock) { return lock->tail != NULL; }

#endif
