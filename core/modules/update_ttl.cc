#include "update_ttl.h"

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/ip.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;

void UpdateTTL::ProcessBatch(bess::PacketBatch *batch) {
  bess::PacketBatch out_batch;
  bess::PacketBatch drop_batch;
  out_batch.clear();
  drop_batch.clear();

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    struct Ethernet *eth = pkt->head_data<struct Ethernet *>();
    struct Ipv4 *ip = reinterpret_cast<struct Ipv4 *>(eth + 1);

    if (ip->ttl > 1) {
      // The incremental checksum only cares the difference from old_value to
      // new_value, so putting 1, 0 than ip->ttl, ip->ttl - 1 offers more
      // optimization opportunities
      ip->checksum =
          bess::utils::CalculateChecksumIncremental16(ip->checksum, 1, 0);
      ip->ttl -= 1;
      out_batch.add(pkt);
    } else {
      drop_batch.add(pkt);  // drop the packet since it's TTL is 1 or 0
    }
  }

  RunChooseModule(0, &out_batch);
  RunChooseModule(1, &drop_batch);
}

ADD_MODULE(UpdateTTL, "update_ttl", "decreases the IP TTL field by 1")
