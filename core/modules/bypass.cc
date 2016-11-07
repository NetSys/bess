#include "bypass.h"

const Commands<Module> Bypass::cmds = {};
const PbCommands Bypass::pb_cmds = {};

void Bypass::ProcessBatch(struct pkt_batch *batch) {
  RunChooseModule(get_igate(), batch);
}

ADD_MODULE(Bypass, "bypass", "bypasses packets without any processing")
