#include "timestamp.h"

#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/time.h"
#include "../utils/udp.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using bess::utils::Udp;

static inline void timestamp_packet(bess::Packet *pkt, size_t offset,
                                    uint64_t time) {
  Timestamp::MarkerType *marker;
  uint64_t *ts;

  const size_t kStampSize = sizeof(*marker) + sizeof(*ts);
  size_t room = pkt->data_len() - offset;

  if (room < kStampSize) {
    void *ret = pkt->append(kStampSize - room);
    if (!ret) {
      // not enough tailroom for timestamp. give up
      return;
    }
  }

  marker = pkt->head_data<Timestamp::MarkerType *>(offset);
  *marker = Timestamp::kMarker;
  ts = reinterpret_cast<uint64_t *>(marker + 1);
  *ts = time;
}

CommandResponse Timestamp::Init(const bess::pb::TimestampArg &arg) {
  if (arg.offset()) {
    offset_ = arg.offset();
  } else {
    offset_ = sizeof(Ethernet) + sizeof(Ipv4) + sizeof(Udp);
  }
  return CommandSuccess();
}

void Timestamp::ProcessBatch(bess::PacketBatch *batch) {
  // We don't use ctx->current_ns here for better accuracy
  uint64_t now_ns = tsc_to_ns(rdtsc());
  size_t offset = offset_;

  for (int i = 0; i < batch->cnt(); i++) {
    timestamp_packet(batch->pkts()[i], offset, now_ns);
  }

  RunNextModule(batch);
}

ADD_MODULE(Timestamp, "timestamp",
           "marks current time to packets (paired with Measure module)")
