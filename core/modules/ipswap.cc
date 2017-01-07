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
    int ip_bytes = (ip->version_ihl & 0xf) << 2;
    struct udp_hdr *udp = reinterpret_cast<struct udp_hdr *>(
        reinterpret_cast<uint8_t *>(ip) + ip_bytes);
    struct tcp_hdr *tcp = reinterpret_cast<struct tcp_hdr *>(udp);
    struct icmp_hdr *icmp = reinterpret_cast<struct icmp_hdr *>(udp);

    // std::swap cannot be used for packed fields
    uint32_t tmp_ip = ip->src_addr;
    ip->src_addr = ip->dst_addr;
    ip->dst_addr = tmp_ip;

    ip->hdr_checksum = 0;

    uint32_t tmp_port;
    switch (ip->next_proto_id) {
      case 0x06:
        tmp_port = udp->src_port;
        udp->src_port = udp->dst_port;
        udp->dst_port = tmp_port;
        tcp->cksum = 0;
        tcp->cksum = rte_ipv4_udptcp_cksum(ip, tcp);
        break;
      case 0x11:
        tmp_port = udp->src_port;
        udp->src_port = udp->dst_port;
        udp->dst_port = tmp_port;
        udp->dgram_cksum = 0;
        break;
      case 0x01:
        icmp->icmp_cksum = 0;
        icmp->icmp_cksum = rte_ipv4_udptcp_cksum(ip, icmp);
        break;
      default:
        VLOG(1) << "Unknown next_proto_id: " << ip->next_proto_id;
    }
    ip->hdr_checksum = rte_ipv4_cksum(ip);
  }

  RunNextModule(batch);
}

ADD_MODULE(IPSwap, "ipswap",
           "swaps source/destination IP addresses and L4 ports")
