#include "merge.h"

const Commands<Module> Merge::cmds = {};

void Merge::ProcessBatch(struct pkt_batch *batch) {
  RunNextModule(batch);
}

ADD_MODULE(Merge, "merge", "All input gates go out of a single output gate")
