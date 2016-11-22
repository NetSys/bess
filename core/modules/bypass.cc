#include "bypass.h"

void Bypass::ProcessBatch(bess::PacketBatch *batch) {
  RunChooseModule(get_igate(), batch);
}

ADD_MODULE(Bypass, "bypass", "bypasses packets without any processing")
