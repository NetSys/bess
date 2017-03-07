#include "url_filter.h"

#include <rte_ip.h>

#include <algorithm>

#include "../utils/ether.h"
#include "../utils/http_parser.h"
#include "../utils/ip.h"

using bess::utils::EthHeader;
using bess::utils::Ipv4Header;
using bess::utils::TcpHeader;

const uint64_t TIME_OUT_NS = 10L * 1000 * 1000 * 1000;  // 10 seconds

const Commands UrlFilter::cmds = {
    {"add", "UrlFilterArg", MODULE_CMD_FUNC(&UrlFilter::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&UrlFilter::CommandClear), 0}};

// Template for generating TCP packets without data
struct[[gnu::packed]] PacketTemplate {
  EthHeader eth;
  Ipv4Header ip;
  TcpHeader tcp;

  PacketTemplate() {
    eth.dst_addr = EthHeader::Address();  // To fill in
    eth.src_addr = EthHeader::Address();  // To fill in
    eth.ether_type = 0x0800;              // IPv4
    ip.version = 4;
    ip.header_length = 5;
    ip.type_of_service = 0;
    ip.length = 40;
    ip.id = 0;
    ip.fragment_offset = 0;
    ip.ttl = 0x40;
    ip.protocol = 0x06;
    ip.checksum = 0;   // To fill in
    ip.src = 0;        // To fill in
    ip.dst = 0;        // To fill in
    tcp.src_port = 0;  // To fill in
    tcp.dst_port = 0;  // To fill in
    tcp.seq_num = 0;   // To fill in
    tcp.ack_num = 0;   // To fill in
    tcp.reserved = 0;
    tcp.offset = 5;
    tcp.flags = 0x14;  // RST-ACK
    tcp.window = 0;
    tcp.checksum = 0;  // To fill in
    tcp.urgent_ptr = 0;
  }
};

static const char *HTTP_HEADER_HOST = "Host";
static const char *HTTP_403_BODY =
    "HTTP/1.1 403 Bad Forbidden\r\nConnection: Closed\r\n\r\n";

static PacketTemplate rst_template;

// Generate an HTTP 403 packet
inline static bess::Packet *Generate403Packet(const EthHeader::Address &src_eth,
                                              const EthHeader::Address &dst_eth,
                                              uint32_t src_ip, uint32_t dst_ip,
                                              uint16_t src_port,
                                              uint16_t dst_port, uint32_t seq,
                                              uint32_t ack) {
  bess::Packet *pkt = bess::Packet::Alloc();
  char *ptr = static_cast<char *>(pkt->buffer()) + SNBUF_HEADROOM;
  pkt->set_data_off(SNBUF_HEADROOM);

  size_t len = strlen(HTTP_403_BODY);
  pkt->set_total_len(sizeof(rst_template) + len);
  pkt->set_data_len(sizeof(rst_template) + len);

  rte_memcpy(ptr, &rst_template, sizeof(rst_template));
  rte_memcpy(ptr + sizeof(rst_template), HTTP_403_BODY, len);

  EthHeader *eth = reinterpret_cast<EthHeader *>(ptr);
  Ipv4Header *ip = reinterpret_cast<Ipv4Header *>(eth + 1);
  // We know there is no IP option
  TcpHeader *tcp = reinterpret_cast<TcpHeader *>(ip + 1);

  eth->dst_addr = dst_eth;
  eth->src_addr = src_eth;
  ip->src = src_ip;
  ip->dst = dst_ip;
  ip->length = 40 + len;
  tcp->src_port = src_port;
  tcp->dst_port = dst_port;
  tcp->seq_num = seq;
  tcp->ack_num = ack;
  tcp->flags = 0x10;  // ACK

  tcp->checksum =
      rte_ipv4_udptcp_cksum(reinterpret_cast<const ipv4_hdr *>(ip), tcp);
  ip->checksum = rte_ipv4_cksum(reinterpret_cast<const ipv4_hdr *>(ip));

  return pkt;
}

// Generate a TCP RST packet
inline static bess::Packet *GenerateResetPacket(
    const EthHeader::Address &src_eth, const EthHeader::Address &dst_eth,
    uint32_t src_ip, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
    uint32_t seq, uint32_t ack) {
  bess::Packet *pkt = bess::Packet::Alloc();
  char *ptr = static_cast<char *>(pkt->buffer()) + SNBUF_HEADROOM;
  pkt->set_data_off(SNBUF_HEADROOM);
  pkt->set_total_len(sizeof(rst_template));
  pkt->set_data_len(sizeof(rst_template));

  rte_memcpy(ptr, &rst_template, sizeof(rst_template));

  EthHeader *eth = reinterpret_cast<EthHeader *>(ptr);
  Ipv4Header *ip = reinterpret_cast<Ipv4Header *>(eth + 1);
  // We know there is no IP option
  TcpHeader *tcp = reinterpret_cast<TcpHeader *>(ip + 1);

  eth->dst_addr = dst_eth;
  eth->src_addr = src_eth;
  ip->src = src_ip;
  ip->dst = dst_ip;
  tcp->src_port = src_port;
  tcp->dst_port = dst_port;
  tcp->seq_num = seq;
  tcp->ack_num = ack;
  tcp->checksum =
      rte_ipv4_udptcp_cksum(reinterpret_cast<const ipv4_hdr *>(ip), tcp);
  ip->checksum = rte_ipv4_cksum(reinterpret_cast<const ipv4_hdr *>(ip));

  return pkt;
}

pb_error_t UrlFilter::Init(const bess::pb::UrlFilterArg &arg) {
  for (const auto &url : arg.blacklist()) {
    if (blacklist_.find(url.host()) == blacklist_.end()) {
      blacklist_.emplace(url.host(), Trie());
    }
    Trie &trie = blacklist_.at(url.host());
    trie.Insert(url.path());
  }
  return pb_errno(0);
}

pb_cmd_response_t UrlFilter::CommandAdd(const bess::pb::UrlFilterArg &arg) {
  pb_cmd_response_t response;
  set_cmd_response_error(&response, Init(arg));
  return response;
}

pb_cmd_response_t UrlFilter::CommandClear(const bess::pb::EmptyArg &) {
  blacklist_.clear();
  return pb_cmd_response_t();
}

void UrlFilter::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t igate = get_igate();

  // Pass reverse traffic
  if (igate == 1) {
    RunChooseModule(1, batch);
    return;
  }

  // Otherwise
  bess::PacketBatch free_batch;
  free_batch.clear();

  bess::PacketBatch out_batches[4];
  // Data to destination
  out_batches[0].clear();
  // RST to destination
  out_batches[1].clear();
  // HTTP 403 to source
  out_batches[2].clear();
  // RST to source
  out_batches[3].clear();

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    struct EthHeader *eth = pkt->head_data<struct EthHeader *>();
    struct Ipv4Header *ip = reinterpret_cast<struct Ipv4Header *>(eth + 1);

    if (ip->protocol != 0x06) {
      out_batches[0].add(pkt);
      continue;
    }

    int ip_bytes = (ip->header_length & 0xf) << 2;
    struct TcpHeader *tcp = reinterpret_cast<struct TcpHeader *>(
        reinterpret_cast<uint8_t *>(ip) + ip_bytes);

    Flow flow;
    flow.src_ip = ip->src;
    flow.dst_ip = ip->dst;
    flow.src_port = tcp->src_port;
    flow.dst_port = tcp->dst_port;

    uint64_t now = ctx.current_ns();

    // Check if the flow is already blocked
    std::unordered_map<Flow, FlowRecord, FlowHash>::iterator it =
        flow_cache_.find(flow);

    if (it != flow_cache_.end()) {
      if (now < it->second.ExpiryTime()) {
        free_batch.add(pkt);
        continue;
      } else {
        flow_cache_.erase(it);
      }
    }

    std::tie(it, std::ignore) = flow_cache_.emplace(
        std::piecewise_construct, std::make_tuple(flow), std::make_tuple());

    FlowRecord &record = it->second;
    TcpFlowReconstruct &buffer = record.GetBuffer();

    // If the reconstruct code indicates failure, treat it as a flow to pass.
    // No need to parse the headers if the reconstruct code tells us it failed.
    bool success = buffer.InsertPacket(pkt);
    if (!success) {
      DLOG(WARNING) << "Reconstruction failure";
      out_batches[0].add(pkt);
      continue;
    }

    bool matched = false;
    struct phr_header headers[16];
    size_t num_headers = 16, method_len, path_len;
    int minor_version;
    const char *method, *path;
    int parse_result = phr_parse_request(
        buffer.buf(), buffer.contiguous_len(), &method, &method_len, &path,
        &path_len, &minor_version, headers, &num_headers, 0);

    // -2 means incomplete
    if (parse_result > 0 || parse_result == -2) {
      const std::string path_str(path, path_len);

      // Look for the Host header
      for (size_t j = 0; j < num_headers && !matched; ++j) {
        if (strncmp(headers[j].name, HTTP_HEADER_HOST, headers[j].name_len) ==
            0) {
          const std::string host(headers[j].value, headers[j].value_len);
          const auto rule_iterator = blacklist_.find(host);
          matched = rule_iterator != blacklist_.end() &&
                    rule_iterator->second.LookupKey(path_str);
        }
      }
    }

    if (!matched) {
      out_batches[0].add(pkt);

      // If FIN is observed, no need to reconstruct this flow
      // NOTE: if FIN is lost on its way to destination, this will simply pass
      // the retransmitted packet
      if (tcp->flags & TCP_FLAG_FIN) {
        flow_cache_.erase(it);
      }
    } else {
      // Block this flow for TIME_OUT_NS nanoseconds
      record.SetExpiryTime(now + TIME_OUT_NS);

      // Inject RST to destination
      out_batches[1].add(GenerateResetPacket(
          eth->src_addr, eth->dst_addr, ip->src, ip->dst, tcp->src_port,
          tcp->dst_port, tcp->seq_num, tcp->ack_num));

      // Inject 403 to source. 403 should arrive earlier than RST.
      out_batches[2].add(Generate403Packet(
          eth->dst_addr, eth->src_addr, ip->dst, ip->src, tcp->dst_port,
          tcp->src_port, tcp->ack_num, tcp->seq_num));

      // Inject RST to source
      out_batches[3].add(GenerateResetPacket(
          eth->dst_addr, eth->src_addr, ip->dst, ip->src, tcp->dst_port,
          tcp->src_port, tcp->ack_num + strlen(HTTP_403_BODY), tcp->seq_num));

      // Drop the data packet
      free_batch.add(pkt);
    }
  }

  bess::Packet::Free(&free_batch);

  RunChooseModule(0, &out_batches[0]);
  RunChooseModule(0, &out_batches[1]);
  RunChooseModule(1, &out_batches[2]);
  RunChooseModule(1, &out_batches[3]);
}

ADD_MODULE(UrlFilter, "url-filter", "Filter HTTP connection")
