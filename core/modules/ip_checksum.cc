#include "ip_checksum.h"

#include <rte_ether.h>
#include <rte_ip.h>

void IPChecksum::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    struct ether_hdr *eth = pkt->head_data<struct ether_hdr *>();
    struct ipv4_hdr *ip = reinterpret_cast<struct ipv4_hdr *>(eth + 1);

    ip->hdr_checksum = 0;

    ip->hdr_checksum = rte_ipv4_cksum(ip);
  }

  RunNextModule(batch);
}

ADD_MODULE(IPChecksum, "ip_checksum", "recomputes the IPv4 checksum")
