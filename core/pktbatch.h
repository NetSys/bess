#ifndef BESS_PKTBATCH_H_
#define BESS_PKTBATCH_H_

#include <rte_memcpy.h>

#define MAX_PKT_BURST 32

namespace bess {

class Packet;

struct pkt_batch {
  int cnt;
  Packet *pkts[MAX_PKT_BURST];
};

static inline void batch_clear(struct pkt_batch *batch) {
  batch->cnt = 0;
}

static inline void batch_add(struct pkt_batch *batch, Packet *pkt) {
  batch->pkts[batch->cnt++] = pkt;
}

static inline int batch_full(struct pkt_batch *batch) {
  return (batch->cnt == MAX_PKT_BURST);
}

static inline void batch_copy(struct pkt_batch *dst,
                              const struct pkt_batch *src) {
  int cnt = src->cnt;

  dst->cnt = cnt;
  rte_memcpy((void *)dst->pkts, (void *)src->pkts, cnt * sizeof(Packet *));
}

}  // namespace bess

#endif  // BESS_PKTBATCH_H_
