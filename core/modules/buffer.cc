#include "buffer.h"

void Buffer::Deinit() {
  struct bess::pkt_batch *buf = &buf_;

  if (buf->cnt) {
    bess::Packet::free_bulk(buf->pkts, buf->cnt);
  }
}

void Buffer::ProcessBatch(struct bess::pkt_batch *batch) {
  struct bess::pkt_batch *buf = &buf_;

  int free_slots = MAX_PKT_BURST - buf->cnt;
  int left = batch->cnt;

  bess::PacketArray p_buf = &buf->pkts[buf->cnt];
  bess::PacketArray p_batch = &batch->pkts[0];

  if (left >= free_slots) {
    buf->cnt = MAX_PKT_BURST;
    rte_memcpy(reinterpret_cast<void *>(p_buf),
               reinterpret_cast<void *>(p_batch),
               free_slots * sizeof(bess::Packet *));

    p_buf = &buf->pkts[0];
    p_batch += free_slots;
    left -= free_slots;

    RunNextModule(buf);
    batch_clear(buf);
  }

  buf->cnt += left;
  rte_memcpy(reinterpret_cast<void *>(p_buf), reinterpret_cast<void *>(p_batch),
             left * sizeof(bess::Packet *));
}

ADD_MODULE(Buffer, "buffer", "buffers packets into larger batches")
