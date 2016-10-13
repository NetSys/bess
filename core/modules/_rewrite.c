#include "../module.h"

#define SLOTS (MAX_PKT_BURST * 2 - 1)
#define MAX_TEMPLATE_SIZE 1536

class Rewrite : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const std::vector<struct Command> cmds;

 private:
  inline void DoRewrite(struct pkt_batch *batch);
  inline void DoRewriteSingle(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  /* For fair round robin we remember the next index for later.
   * [0, num_templates - 1] */
  int next_turn;

  int num_templates;
  uint16_t template_size[SLOTS];
  unsigned char templates[SLOTS][MAX_TEMPLATE_SIZE] __ymm_aligned;
};

const std::vector<struct Command> Rewrite::cmds = {
    {"add", static_cast<CmdFunc>(&Rewrite::CommandAdd), 0},
    {"clear", static_cast<CmdFunc>(&Rewrite::CommandClear), 0},
};

struct snobj *Rewrite::Init(struct snobj *arg) {
  struct snobj *t;

  if (!arg) return NULL;

  if (!(t = snobj_eval(arg, "templates")))
    return snobj_err(EINVAL, "'templates' must be specified");

  return this->CommandAdd(t);
}

inline void Rewrite::DoRewriteSingle(struct pkt_batch *batch) {
  const int cnt = batch->cnt;
  uint16_t size = this->template_size[0];
  void *templ = this->templates[0];

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
  int start = this->next_turn;
  const int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    uint16_t size = this->template_size[start + i];
    struct snbuf *pkt = batch->pkts[i];
    char *ptr = static_cast<char *>(pkt->mbuf.buf_addr) + SNBUF_HEADROOM;

    pkt->mbuf.data_off = SNBUF_HEADROOM;
    pkt->mbuf.pkt_len = size;
    pkt->mbuf.data_len = size;

    memcpy_sloppy(ptr, this->templates[start + i], size);
  }

  this->next_turn = (start + cnt) % this->num_templates;
}

void Rewrite::ProcessBatch(struct pkt_batch *batch) {
  if (this->num_templates == 1)
    this->DoRewriteSingle(batch);
  else if (this->num_templates > 1)
    this->DoRewrite(batch);

  run_next_module(this, batch);
}

struct snobj *Rewrite::CommandAdd(struct snobj *arg) {
  int curr = this->num_templates;
  int i;

  if (snobj_type(arg) != TYPE_LIST)
    return snobj_err(EINVAL, "argument must be a list");

  if (curr + arg->size > MAX_PKT_BURST)
    return snobj_err(EINVAL,
                     "max %d packet templates "
                     "can be used",
                     MAX_PKT_BURST);

  for (i = 0; i < static_cast<int>(arg->size); i++) {
    struct snobj *templ = snobj_list_get(arg, i);

    if (templ->type != TYPE_BLOB)
      return snobj_err(EINVAL,
                       "packet template "
                       "should be BLOB type");

    if (templ->size > MAX_TEMPLATE_SIZE)
      return snobj_err(EINVAL, "template is too big");

    memset(this->templates[curr + i], 0, MAX_TEMPLATE_SIZE);
    memcpy(this->templates[curr + i], snobj_blob_get(templ), templ->size);
    this->template_size[curr + i] = templ->size;
  }

  this->num_templates = curr + arg->size;

  for (i = this->num_templates; i < SLOTS; i++) {
    int j = i % this->num_templates;
    memcpy(this->templates[i], this->templates[j], this->template_size[j]);
    this->template_size[i] = this->template_size[j];
  }

  return NULL;
}

struct snobj *Rewrite::CommandClear(struct snobj *arg) {
  this->next_turn = 0;
  this->num_templates = 0;

  return NULL;
}

ModuleClassRegister<Rewrite> rewrite("Rewrite", "rewrite",
                                     "replaces entire packet data");
