#include "timestamp.h"

#include <rte_config.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "../utils/time.h"

static inline void timestamp_packet(bess::Packet *pkt, size_t offset,
                                    uint64_t time) {
  uint8_t *avail = pkt->head_data<uint8_t *>() + offset;
  *avail = 1;
  uint64_t *ts = reinterpret_cast<uint64_t *>(avail + 1);
  *ts = time;
}

pb_error_t Timestamp::Init(const bess::pb::TimestampArg &arg) {
  if (arg.offset()) {
    offset_ = arg.offset();
  } else {
    offset_ = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) +
              sizeof(struct tcp_hdr);
  }
  return pb_errno(0);
}

void Timestamp::ProcessBatch(bess::PacketBatch *batch) {
  uint64_t time = tsc_to_ns(rdtsc());
  size_t offset = offset_;

  for (int i = 0; i < batch->cnt(); i++) {
    timestamp_packet(batch->pkts()[i], offset, time);
  }

  RunNextModule(batch);
}

ADD_MODULE(Timestamp, "timestamp",
           "marks current time to packets (paired with Measure module)")
