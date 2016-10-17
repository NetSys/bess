#include "../module.h"

/* TODO: timer-triggered flush */
class Buffer : public Module {
 public:
  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const std::vector<struct Command> cmds;

 private:
  struct pkt_batch buf_ = {0};
};

const std::vector<struct Command> Buffer::cmds = {};

void Buffer::Deinit() {
  struct pkt_batch *buf = &this->buf_;

  if (buf->cnt) snb_free_bulk(buf->pkts, buf->cnt);
}

void Buffer::ProcessBatch(struct pkt_batch *batch) {
  struct pkt_batch *buf = &this->buf_;

  int free_slots = MAX_PKT_BURST - buf->cnt;
  int left = batch->cnt;

  snb_array_t p_buf = &buf->pkts[buf->cnt];
  snb_array_t p_batch = &batch->pkts[0];

  if (left >= free_slots) {
    buf->cnt = MAX_PKT_BURST;
    rte_memcpy((void *)p_buf, (void *)p_batch,
               free_slots * sizeof(struct snbuf *));

    p_buf = &buf->pkts[0];
    p_batch += free_slots;
    left -= free_slots;

    run_next_module(this, buf);
    batch_clear(buf);
  }

  buf->cnt += left;
  rte_memcpy((void *)p_buf, (void *)p_batch, left * sizeof(struct snbuf *));
}

ADD_MODULE(Buffer, "buffer", "buffers packets into larger batches")
