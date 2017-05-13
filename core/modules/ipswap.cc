#include "ipswap.h"

#include "utils/ether.h"
#include "utils/ip.h"
#include "utils/udp.h"

void IPSwap::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Udp;

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
    size_t ip_bytes = (ip->header_length & 0xf) << 2;
    Udp *udp =
        reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);

    // std::swap cannot be used for packed fields
    bess::utils::be32_t tmp_ip = ip->src;
    ip->src = ip->dst;
    ip->dst = tmp_ip;

    bess::utils::be16_t tmp_port;
    switch (ip->protocol) {
      case Ipv4::Proto::kTcp:
      case Ipv4::Proto::kUdp:
        // TCP and UDP share the same layout for ports
        tmp_port = udp->src_port;
        udp->src_port = udp->dst_port;
        udp->dst_port = tmp_port;
        break;
      case Ipv4::Proto::kIcmp:
        break;
      default:
        VLOG(1) << "Unknown protocol: " << ip->protocol;
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(IPSwap, "ipswap",
           "swaps source/destination IP addresses and L4 ports")
