#include "bypass.h"

void Bypass::ProcessBatch(struct bess::pkt_batch *batch) {
  RunChooseModule(get_igate(), batch);
}

ADD_MODULE(Bypass, "bypass", "bypasses packets without any processing")
