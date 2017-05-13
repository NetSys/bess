#include "ip_checksum.h"

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/ip.h"

void IPChecksum::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    Ethernet *eth = batch->pkts()[i]->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
    ip->checksum = CalculateIpv4NoOptChecksum(*ip);
  }

  RunNextModule(batch);
}

ADD_MODULE(IPChecksum, "ip_checksum", "recomputes the IPv4 checksum")
