#include "rewrite.h"

const Commands<Module> Rewrite::cmds = {
    {"add", MODULE_FUNC &Rewrite::CommandAdd, 0},
    {"clear", MODULE_FUNC &Rewrite::CommandClear, 0},
};

const PbCommands Rewrite::pb_cmds = {
    {"add", MODULE_CMD_FUNC(&Rewrite::CommandAddPb), 0},
    {"clear", MODULE_CMD_FUNC(&Rewrite::CommandClearPb), 0},
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

pb_error_t Rewrite::InitPb(const bess::pb::RewriteArg &arg) {
  bess::pb::ModuleCommandResponse response = CommandAddPb(arg);
  return response.error();
}

bess::pb::ModuleCommandResponse Rewrite::CommandAddPb(
    const bess::pb::RewriteArg &arg) {
  bess::pb::ModuleCommandResponse response;

  int curr = num_templates_;
  int i;

  if (curr + arg.templates_size() > MAX_PKT_BURST) {
    set_cmd_response_error(&response,
                           pb_error(EINVAL,
                                    "max %d packet templates "
                                    "can be used %d %d",
                                    MAX_PKT_BURST, curr, arg.templates_size()));
    return response;
  }

  for (i = 0; i < arg.templates_size(); i++) {
    const auto &templ = arg.templates(i);

    if (templ.length() > MAX_TEMPLATE_SIZE) {
      set_cmd_response_error(&response,
                             pb_error(EINVAL, "template is too big"));
      return response;
    }

    memset(templates_[curr + i], 0, MAX_TEMPLATE_SIZE);
    memcpy(templates_[curr + i], templ.c_str(), templ.length());
    template_size_[curr + i] = templ.length();
  }

  num_templates_ = curr + arg.templates_size();

  for (i = num_templates_; i < SLOTS; i++) {
    int j = i % num_templates_;
    memcpy(templates_[i], templates_[j], template_size_[j]);
    template_size_[i] = template_size_[j];
  }

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

bess::pb::ModuleCommandResponse Rewrite::CommandClearPb(
    const bess::pb::EmptyArg &) {
  next_turn_ = 0;
  num_templates_ = 0;

  bess::pb::ModuleCommandResponse response;

  set_cmd_response_error(&response, pb_errno(0));
  return response;
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

  RunNextModule(batch);
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

struct snobj *Rewrite::CommandClear(struct snobj *) {
  next_turn_ = 0;
  num_templates_ = 0;

  return nullptr;
}

ADD_MODULE(Rewrite, "rewrite", "replaces entire packet data")
