#include "dump.h"

#include <cmath>
#include <iostream>

#include <rte_hexdump.h>

#define NS_PER_SEC 1000000000ul

static const uint64_t DEFAULT_INTERVAL_NS = 1 * NS_PER_SEC; /* 1 sec */

const Commands Dump::cmds = {
    {"set_interval", "DumpArg", MODULE_CMD_FUNC(&Dump::CommandSetInterval), 0},
};

pb_error_t Dump::Init(const bess::pb::DumpArg &arg) {
  min_interval_ns_ = DEFAULT_INTERVAL_NS;
  next_ns_ = ctx.current_tsc();
  pb_cmd_response_t response = CommandSetInterval(arg);
  return response.error();
}

void Dump::ProcessBatch(bess::PacketBatch *batch) {
  if (unlikely(ctx.current_ns() >= next_ns_)) {
    bess::Packet *pkt = batch->pkts()[0];

    printf("----------------------------------------\n");
    printf("%s: packet dump\n", name().c_str());
    std::cout << pkt->Dump();
    rte_hexdump(stdout, "Metadata buffer", pkt->metadata(), SNBUF_METADATA);
    next_ns_ = ctx.current_ns() + min_interval_ns_;
  }

  RunChooseModule(get_igate(), batch);
}

pb_cmd_response_t Dump::CommandSetInterval(const bess::pb::DumpArg &arg) {
  pb_cmd_response_t response;

  double sec = arg.interval();

  if (std::isnan(sec) || sec <= 0.0) {
    set_cmd_response_error(&response, pb_error(EINVAL, "invalid interval"));
    return response;
  }

  min_interval_ns_ = static_cast<uint64_t>(sec * NS_PER_SEC);

  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

ADD_MODULE(Dump, "dump", "Dump packet data and metadata attributes")
