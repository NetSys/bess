#include "generic_decap.h"

CommandResponse GenericDecap::Init(const bess::pb::GenericDecapArg &arg) {
  if (arg.bytes() == 0) {
    return CommandSuccess();
  }
  decap_size_ = arg.bytes();
  if (decap_size_ <= 0 || decap_size_ > 1024) {
    return CommandFailure(EINVAL, "invalid decap size");
  }
  return CommandSuccess();
}

void GenericDecap::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  int decap_size = decap_size_;

  for (int i = 0; i < cnt; i++) {
    batch->pkts()[i]->adj(decap_size);
  }

  RunNextModule(batch);
}

ADD_MODULE(GenericDecap, "generic_decap",
           "remove specified bytes from the beginning of packets")
