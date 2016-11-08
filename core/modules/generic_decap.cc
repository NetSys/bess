#include "generic_decap.h"

const Commands<Module> GenericDecap::cmds = {};
const PbCommands GenericDecap::pb_cmds = {};

pb_error_t GenericDecap::InitPb(const bess::pb::GenericDecapArg &arg) {
  if (arg.bytes() == 0) {
    return pb_errno(0);
  }
  decap_size_ = arg.bytes();
  if (decap_size_ <= 0 || decap_size_ > 1024) {
    return pb_error(EINVAL, "invalid decap size");
  }
  return pb_errno(0);
}

struct snobj *GenericDecap::Init(struct snobj *arg) {
  if (!arg) {
    return nullptr;
  }

  if (snobj_type(arg) == TYPE_INT) {
    decap_size_ = snobj_uint_get(arg);
  } else if (snobj_type(arg) == TYPE_MAP && snobj_eval_exists(arg, "bytes")) {
    decap_size_ = snobj_eval_uint(arg, "bytes");
  } else {
    return snobj_err(EINVAL, "invalid argument");
  }

  if (decap_size_ <= 0 || decap_size_ > 1024) {
    return snobj_err(EINVAL, "invalid decap size");
  }

  return nullptr;
}

void GenericDecap::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  int decap_size = decap_size_;

  for (int i = 0; i < cnt; i++) {
    snb_adj(batch->pkts[i], decap_size);
  }

  RunNextModule(batch);
}

ADD_MODULE(GenericDecap, "generic_decap",
           "remove specified bytes from the beginning of packets")
