#include "l4_checksum.h"

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/tcp.h"
#include "../utils/udp.h"

void L4Checksum::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Tcp;
  using bess::utils::Udp;
  using bess::utils::be16_t;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    Ethernet *eth = batch->pkts()[i]->head_data<Ethernet *>();

    // Calculate checksum only for IPv4 packets
    if (eth->ether_type != be16_t(Ethernet::Type::kIpv4))
      continue;

    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

    if (ip->protocol == Ipv4::Proto::kUdp) {
      size_t ip_bytes = (ip->header_length) << 2;
      Udp *udp =
          reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);
      udp->checksum = CalculateIpv4UdpChecksum(*ip, *udp);
    } else if (ip->protocol == Ipv4::Proto::kTcp) {
      size_t ip_bytes = (ip->header_length) << 2;
      Tcp *tcp =
          reinterpret_cast<Tcp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);
      tcp->checksum = CalculateIpv4TcpChecksum(*ip, *tcp);
    }

    continue;
  }

  RunNextModule(batch);
}

ADD_MODULE(L4Checksum, "l4_checksum",
           "recomputes the TCP/Ipv4 and UDP/IPv4 checksum")
