#include "update_ttl.h"
#include "../utils/ether.h"
#include "../utils/ip.h"
//#include <netinet/in.h> // This import is needed for the checksum code

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
      /* Additional code for efficient checksum update for  changing TTL by an
       * amount n
       * TODO: Integration - add a check to identify an existing checksum?
       * WARNING: though this code is efficient it can only update an already
       * calculated checksum
       * this code cannot calculate a checksum from scratch like in
       * ip_checksum.cc
       */

      //            unsigned long sum;
      //            unsigned short old;

      //            old = ~ntohs(*(unsigned short *)&ip->ttl);
      ip->ttl -= 1;  // TODO: make this a customizable parameter
      //            sum = ntohs(ip->checksum) - old - (ntohs(*(unsigned short
      //            *)&ip->ttl) & 0xffff);
      //            sum = (sum & 0xffff) + (sum>>16);
      //            ip->checksum = htons(sum + (sum>>16));
      out_batch.add(pkt);
    } else {
      free_batch.add(pkt);  // drop the packet since it's TTL is 1 or 0
    }
  }
  if (!free_batch.empty()) {
    bess::Packet::Free(&free_batch);
  }
  RunNextModule(&out_batch);
}

ADD_MODULE(UpdateTTL, "update_ttl", "updates ttl")
