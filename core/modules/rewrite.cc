#include "../module.h"

#define SLOTS (MAX_PKT_BURST * 2 - 1)
#define MAX_TEMPLATE_SIZE 1536

class Rewrite : public Module {
 public:
  Rewrite() :
      Module(),
      next_turn_(),
      num_templates_(),
      template_size_(),
      templates_() {}

  virtual struct snobj *Init(struct snobj *arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const Commands<Rewrite> cmds;

 private:
  inline void DoRewrite(struct pkt_batch *batch);
  inline void DoRewriteSingle(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  /* For fair round robin we remember the next index for later.
   * [0, num_templates - 1] */
  int next_turn_;

  int num_templates_;
  uint16_t template_size_[SLOTS];
  unsigned char templates_[SLOTS][MAX_TEMPLATE_SIZE] __ymm_aligned;
};

const Commands<Rewrite> Rewrite::cmds = {
    {"add", &Rewrite::CommandAdd, 0}, {"clear", &Rewrite::CommandClear, 0},
};

struct snobj *Rewrite::Init(struct snobj *arg) {
  struct snobj *t;

  if (!arg) {
    return nullptr;
  }

  if (!(t = snobj_eval(arg, "templates"))) {
    return snobj_err(EINVAL, "'templates' must be specified");
  }

  return CommandAdd(t);
}

inline void Rewrite::DoRewriteSingle(struct pkt_batch *batch) {
  const int cnt = batch->cnt;
  uint16_t size = template_size_[0];
  void *templ = templates_[0];

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];
    char *ptr = static_cast<char *>(pkt->mbuf.buf_addr) + SNBUF_HEADROOM;

    pkt->mbuf.data_off = SNBUF_HEADROOM;
    pkt->mbuf.pkt_len = size;
    pkt->mbuf.data_len = size;

    memcpy_sloppy(ptr, templ, size);
  }
}

inline void Rewrite::DoRewrite(struct pkt_batch *batch) {
  int start = next_turn_;
  const int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    uint16_t size = template_size_[start + i];
    struct snbuf *pkt = batch->pkts[i];
    char *ptr = static_cast<char *>(pkt->mbuf.buf_addr) + SNBUF_HEADROOM;

    pkt->mbuf.data_off = SNBUF_HEADROOM;
    pkt->mbuf.pkt_len = size;
    pkt->mbuf.data_len = size;

    memcpy_sloppy(ptr, templates_[start + i], size);
  }

  next_turn_ = (start + cnt) % num_templates_;
}

void Rewrite::ProcessBatch(struct pkt_batch *batch) {
  if (num_templates_ == 1) {
    DoRewriteSingle(batch);
  } else if (num_templates_ > 1) {
    DoRewrite(batch);
  }

  run_next_module(this, batch);
}

struct snobj *Rewrite::CommandAdd(struct snobj *arg) {
  int curr = num_templates_;
  int i;

  if (snobj_type(arg) != TYPE_LIST) {
    return snobj_err(EINVAL, "argument must be a list");
  }

  if (curr + arg->size > MAX_PKT_BURST) {
    return snobj_err(EINVAL,
                     "max %d packet templates "
                     "can be used %d %d",
                     MAX_PKT_BURST, curr, arg->size);
  }

  for (i = 0; i < static_cast<int>(arg->size); i++) {
    struct snobj *templ = snobj_list_get(arg, i);

    if (templ->type != TYPE_BLOB) {
      return snobj_err(EINVAL,
                       "packet template "
                       "should be BLOB type");
    }

    if (templ->size > MAX_TEMPLATE_SIZE) {
      return snobj_err(EINVAL, "template is too big");
    }

    memset(templates_[curr + i], 0, MAX_TEMPLATE_SIZE);
    memcpy(templates_[curr + i], snobj_blob_get(templ), templ->size);
    template_size_[curr + i] = templ->size;
  }

  num_templates_ = curr + arg->size;

  for (i = num_templates_; i < SLOTS; i++) {
    int j = i % num_templates_;
    memcpy(templates_[i], templates_[j], template_size_[j]);
    template_size_[i] = template_size_[j];
  }

  return nullptr;
}

struct snobj *Rewrite::CommandClear(struct snobj *arg) {
  next_turn_ = 0;
  num_templates_ = 0;

  return nullptr;
}

ADD_MODULE(Rewrite, "rewrite", "replaces entire packet data")
