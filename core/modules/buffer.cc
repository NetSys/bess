#include "buffer.h"

void Buffer::DeInit() {
  bess::PacketBatch *buf = &buf_;
  bess::Packet::Free(buf);
}

void Buffer::ProcessBatch(bess::PacketBatch *batch) {
  bess::PacketBatch *buf = &buf_;

  int free_slots = bess::PacketBatch::kMaxBurst - buf->cnt();
  int left = batch->cnt();

  bess::Packet **p_buf = &buf->pkts()[buf->cnt()];
  bess::Packet **p_batch = &batch->pkts()[0];

  if (left >= free_slots) {
    buf->set_cnt(bess::PacketBatch::kMaxBurst);
    bess::utils::CopyInlined(p_buf, p_batch,
                             free_slots * sizeof(bess::Packet *));

    p_buf = &buf->pkts()[0];
    p_batch += free_slots;
    left -= free_slots;

    RunNextModule(buf);
    buf->clear();
  }

  buf->incr_cnt(left);
  bess::utils::CopyInlined(p_buf, p_batch, left * sizeof(bess::Packet *));
}

ADD_MODULE(Buffer, "buffer", "buffers packets into larger batches")
