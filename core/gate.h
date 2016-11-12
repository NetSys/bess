#ifndef BESS_GATE_H_
#define BESS_GATE_H_

#include "utils/cdlist.h"

class Module;

typedef uint16_t gate_idx_t;

#define TRACK_GATES 1
#define TCPDUMP_GATES 1

#define INVALID_GATE UINT16_MAX

/* A module may have up to MAX_GATES input/output gates (separately). */
#define MAX_GATES 8192
#define DROP_GATE MAX_GATES
static_assert(MAX_GATES < INVALID_GATE, "invalid macro value");
static_assert(DROP_GATE <= MAX_GATES, "invalid macro value");

struct gate {
  /* immutable values */
  Module *m;           /* the module this gate belongs to */
  gate_idx_t gate_idx; /* input/output gate index of itself */
                       /*  index is relative to module, not global */
  /* mutable values below */
  void *arg;

  union {
    struct {
      struct cdlist_item igate_upstream; /* next and prev module*/
      struct gate *igate;                /* self */
      gate_idx_t igate_idx;              /* cache for igate->gate_idx */
    } out;

    struct {
      struct cdlist_head ogates_upstream;
    } in;
  };

/* TODO: generalize with gate hooks */
#if TRACK_GATES
  uint64_t cnt;
  uint64_t pkts;
#endif
#if TCPDUMP_GATES
  uint32_t tcpdump;
  int fifo_fd;
#endif
};

struct gates {
  /* Resizable array of 'struct gate *'.
   * Unconnected elements are filled with nullptr */
  struct gate **arr;

  /* The current size of the array.
   * Always <= m->mclass->num_[i|o]gates */
  gate_idx_t curr_size;
};

#endif
