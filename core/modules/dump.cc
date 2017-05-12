#include "dump.h"

#include <cmath>
#include <iostream>

#include <rte_hexdump.h>

#define NS_PER_SEC 1000000000ull

static const uint64_t DEFAULT_INTERVAL_NS = 1 * NS_PER_SEC; /* 1 sec */

const Commands Dump::cmds = {
    {"set_interval", "DumpArg", MODULE_CMD_FUNC(&Dump::CommandSetInterval), 0},
};

CommandResponse Dump::Init(const bess::pb::DumpArg &arg) {
  min_interval_ns_ = DEFAULT_INTERVAL_NS;
  next_ns_ = ctx.current_tsc();
  return CommandSetInterval(arg);
}

void Dump::ProcessBatch(bess::PacketBatch *batch) {
  if (unlikely(ctx.current_ns() >= next_ns_)) {
    bess::Packet *pkt = batch->pkts()[0];

    printf("----------------------------------------\n");
    printf("%s: packet dump\n", name().c_str());
    std::cout << pkt->Dump();
    rte_hexdump(stdout, "Metadata buffer", pkt->metadata<const char *>(),
                SNBUF_METADATA);
    next_ns_ = ctx.current_ns() + min_interval_ns_;
  }

  RunChooseModule(get_igate(), batch);
}

CommandResponse Dump::CommandSetInterval(const bess::pb::DumpArg &arg) {
  double sec = arg.interval();

  if (std::isnan(sec) || sec <= 0.0) {
    return CommandFailure(EINVAL, "invalid interval");
  }

  min_interval_ns_ = static_cast<uint64_t>(sec * NS_PER_SEC);
  return CommandSuccess();
}

ADD_MODULE(Dump, "dump", "Dump packet data and metadata attributes")
