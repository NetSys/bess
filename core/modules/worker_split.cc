#include "worker_split.h"

void WorkerSplit::ProcessBatch(bess::PacketBatch *batch) {
  RunChooseModule(ctx.wid(), batch);
}

ADD_MODULE(WorkerSplit, "ws",
           "send packets to output gate X, the id of current worker")
