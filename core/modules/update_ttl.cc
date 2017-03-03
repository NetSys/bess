#include "update_ttl.h"
#include "../utils/ether.h"
#include "../utils/ip.h"

using bess::utils::EthHeader;
using bess::utils::Ipv4Header;

void UpdateTTL::ProcessBatch(bess::PacketBatch *batch) {
  bess::PacketBatch out_batch;
  bess::PacketBatch free_batch;
  out_batch.clear();
  free_batch.clear();

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    struct EthHeader *eth = pkt->head_data<struct EthHeader *>();
    struct Ipv4Header *ip = reinterpret_cast<struct Ipv4Header *>(eth + 1);

    if (ip->ttl > 1) {
      // Current design choice: implement a checksum at a downstream module
      // instead of updating here. If efficent code for checksumming at each
      // module is a future change, RFC 1624 will be helpful for implementation
      // in this module.
      ip->ttl -= 1;  // TODO: make this a customizable parameter
      out_batch.add(pkt);
    } else {
      free_batch.add(pkt);  // drop the packet since it's TTL is 1 or 0
    }
  }

  bess::Packet::Free(&free_batch);
  RunNextModule(&out_batch);
}

ADD_MODULE(UpdateTTL, "update_ttl", "decreases the IP TTL field by 1")
