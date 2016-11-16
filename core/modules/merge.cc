#include "merge.h"

void Merge::ProcessBatch(struct bess::pkt_batch *batch) {
  RunNextModule(batch);
}

ADD_MODULE(Merge, "merge", "All input gates go out of a single output gate")
