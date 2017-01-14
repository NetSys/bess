#include "ipswap.h"

#include <rte_ether.h>
#include <rte_icmp.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

void IPSwap::ProcessBatch(bess::PacketBatch *batch) {
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    struct ether_hdr *eth = pkt->head_data<struct ether_hdr *>();
    struct ipv4_hdr *ip = reinterpret_cast<struct ipv4_hdr *>(eth + 1);
    size_t ip_bytes = (ip->version_ihl & 0xf) << 2;
    struct udp_hdr *udp = reinterpret_cast<struct udp_hdr *>(
        reinterpret_cast<uint8_t *>(ip) + ip_bytes);

    // std::swap cannot be used for packed fields
    uint32_t tmp_ip = ip->src_addr;
    ip->src_addr = ip->dst_addr;
    ip->dst_addr = tmp_ip;

    uint16_t tmp_port;
    switch (ip->next_proto_id) {
      case 0x06:  // TCP
      case 0x11:  // UDP
        // TCP and UDP share the same layout for ports
        tmp_port = udp->src_port;
        udp->src_port = udp->dst_port;
        udp->dst_port = tmp_port;
        break;
      case 0x01:  // ICMP
        break;
      default:
        VLOG(1) << "Unknown next_proto_id: " << ip->next_proto_id;
    }
  }

  RunNextModule(batch);
}

ADD_MODULE(IPSwap, "ipswap",
           "swaps source/destination IP addresses and L4 ports")
