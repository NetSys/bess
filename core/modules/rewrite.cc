#include "rewrite.h"

#include <cstdio>

#include "../utils/copy.h"

const Commands Rewrite::cmds = {
    {"add", "RewriteArg", MODULE_CMD_FUNC(&Rewrite::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&Rewrite::CommandClear), 0},
};

CommandResponse Rewrite::Init(const bess::pb::RewriteArg &arg) {
  return CommandAdd(arg);
}

CommandResponse Rewrite::CommandAdd(const bess::pb::RewriteArg &arg) {
  size_t curr = num_templates_;

  if (curr + arg.templates_size() > bess::PacketBatch::kMaxBurst) {
    return CommandFailure(EINVAL, "max %zu packet templates can be used %zu %d",
                          bess::PacketBatch::kMaxBurst, curr,
                          arg.templates_size());
  }

  for (int i = 0; i < arg.templates_size(); i++) {
    const auto &templ = arg.templates(i);

    if (templ.length() > kMaxTemplateSize) {
      return CommandFailure(EINVAL, "template is too big");
    }

    memset(templates_[curr + i], 0, kMaxTemplateSize);
    bess::utils::Copy(templates_[curr + i], templ.c_str(), templ.length());
    template_size_[curr + i] = templ.length();
  }

  num_templates_ = curr + arg.templates_size();
  if (num_templates_ == 0) {
    return CommandSuccess();
  }

  for (size_t i = num_templates_; i < kNumSlots; i++) {
    size_t j = i % num_templates_;
    bess::utils::Copy(templates_[i], templates_[j], template_size_[j]);
    template_size_[i] = template_size_[j];
  }

  return CommandSuccess();
}

CommandResponse Rewrite::CommandClear(const bess::pb::EmptyArg &) {
  next_turn_ = 0;
  num_templates_ = 0;
  return CommandSuccess();
}

inline void Rewrite::DoRewriteSingle(bess::PacketBatch *batch) {
  const int cnt = batch->cnt();
  uint16_t size = template_size_[0];
  const void *templ = templates_[0];

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    char *ptr = pkt->buffer<char *>() + SNBUF_HEADROOM;

    pkt->set_data_off(SNBUF_HEADROOM);
    pkt->set_total_len(size);
    pkt->set_data_len(size);

    bess::utils::CopyInlined(ptr, templ, size, true);
  }
}

inline void Rewrite::DoRewrite(bess::PacketBatch *batch) {
  size_t start = next_turn_;
  const size_t cnt = batch->cnt();

  for (size_t i = 0; i < cnt; i++) {
    uint16_t size = template_size_[start + i];
    bess::Packet *pkt = batch->pkts()[i];
    char *ptr = pkt->buffer<char *>() + SNBUF_HEADROOM;

    pkt->set_data_off(SNBUF_HEADROOM);
    pkt->set_total_len(size);
    pkt->set_data_len(size);

    bess::utils::CopyInlined(ptr, templates_[start + i], size, true);
  }

  next_turn_ = start + cnt;
  if (next_turn_ >= bess::PacketBatch::kMaxBurst) {
    next_turn_ -= bess::PacketBatch::kMaxBurst;
  }
}

void Rewrite::ProcessBatch(bess::PacketBatch *batch) {
  if (num_templates_ == 1) {
    DoRewriteSingle(batch);
  } else if (num_templates_ > 1) {
    DoRewrite(batch);
  }

  RunNextModule(batch);
}

ADD_MODULE(Rewrite, "rewrite", "replaces entire packet data")
