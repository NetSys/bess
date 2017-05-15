#include "url_filter.h"

#include <algorithm>

#include "../utils/checksum.h"
#include "../utils/ether.h"
#include "../utils/http_parser.h"
#include "../utils/ip.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using bess::utils::Tcp;
using bess::utils::be16_t;

const uint64_t TIME_OUT_NS = 10ull * 1000 * 1000 * 1000;  // 10 seconds

const Commands UrlFilter::cmds = {
    {"add", "UrlFilterArg", MODULE_CMD_FUNC(&UrlFilter::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&UrlFilter::CommandClear), 0}};

// Template for generating TCP packets without data
struct[[gnu::packed]] PacketTemplate {
  Ethernet eth;
  Ipv4 ip;
  Tcp tcp;

  PacketTemplate() {
    eth.dst_addr = Ethernet::Address();  // To fill in
    eth.src_addr = Ethernet::Address();  // To fill in
    eth.ether_type = be16_t(Ethernet::Type::kIpv4);
    ip.version = 4;
    ip.header_length = 5;
    ip.type_of_service = 0;
    ip.length = be16_t(40);
    ip.id = be16_t(0);
    ip.fragment_offset = be16_t(0);
    ip.ttl = 0x40;
    ip.protocol = Ipv4::Proto::kTcp;
    ip.checksum = 0;           // To fill in
    ip.src = be32_t(0);        // To fill in
    ip.dst = be32_t(0);        // To fill in
    tcp.src_port = be16_t(0);  // To fill in
    tcp.dst_port = be16_t(0);  // To fill in
    tcp.seq_num = be32_t(0);   // To fill in
    tcp.ack_num = be32_t(0);   // To fill in
    tcp.reserved = 0;
    tcp.offset = 5;
    tcp.flags = Tcp::Flag::kAck | Tcp::Flag::kRst;
    tcp.window = be16_t(0);
    tcp.checksum = 0;  // To fill in
    tcp.urgent_ptr = be16_t(0);
  }
};

static const char *HTTP_HEADER_HOST = "Host";
static const char *HTTP_403_BODY =
    "HTTP/1.1 403 Bad Forbidden\r\nConnection: Closed\r\n\r\n";

static PacketTemplate rst_template;

// Generate an HTTP 403 packet
inline static bess::Packet *Generate403Packet(const Ethernet::Address &src_eth,
                                              const Ethernet::Address &dst_eth,
                                              be32_t src_ip, be32_t dst_ip,
                                              be16_t src_port, be16_t dst_port,
                                              be32_t seq, be32_t ack) {
  bess::Packet *pkt = bess::Packet::Alloc();
  char *ptr = static_cast<char *>(pkt->buffer()) + SNBUF_HEADROOM;
  pkt->set_data_off(SNBUF_HEADROOM);

  size_t len = strlen(HTTP_403_BODY);
  pkt->set_total_len(sizeof(rst_template) + len);
  pkt->set_data_len(sizeof(rst_template) + len);

  bess::utils::Copy(ptr, &rst_template, sizeof(rst_template));
  bess::utils::Copy(ptr + sizeof(rst_template), HTTP_403_BODY, len, true);

  Ethernet *eth = reinterpret_cast<Ethernet *>(ptr);
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
  // We know there is no IP option
  Tcp *tcp = reinterpret_cast<Tcp *>(ip + 1);

  eth->dst_addr = dst_eth;
  eth->src_addr = src_eth;
  ip->src = src_ip;
  ip->dst = dst_ip;
  ip->length = be16_t(40 + len);
  tcp->src_port = src_port;
  tcp->dst_port = dst_port;
  tcp->seq_num = seq;
  tcp->ack_num = ack;
  tcp->flags = Tcp::Flag::kAck;

  tcp->checksum = bess::utils::CalculateIpv4TcpChecksum(*tcp, src_ip, dst_ip,
                                                        sizeof(*tcp) + len);
  ip->checksum = bess::utils::CalculateIpv4NoOptChecksum(*ip);

  return pkt;
}

// Generate a TCP RST packet
inline static bess::Packet *GenerateResetPacket(
    const Ethernet::Address &src_eth, const Ethernet::Address &dst_eth,
    be32_t src_ip, be32_t dst_ip, be16_t src_port, be16_t dst_port, be32_t seq,
    be32_t ack) {
  bess::Packet *pkt = bess::Packet::Alloc();
  char *ptr = static_cast<char *>(pkt->buffer()) + SNBUF_HEADROOM;
  pkt->set_data_off(SNBUF_HEADROOM);
  pkt->set_total_len(sizeof(rst_template));
  pkt->set_data_len(sizeof(rst_template));

  bess::utils::Copy(ptr, &rst_template, sizeof(rst_template), true);

  Ethernet *eth = reinterpret_cast<Ethernet *>(ptr);
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
  // We know there is no IP option
  Tcp *tcp = reinterpret_cast<Tcp *>(ip + 1);

  eth->dst_addr = dst_eth;
  eth->src_addr = src_eth;
  ip->src = src_ip;
  ip->dst = dst_ip;
  tcp->src_port = src_port;
  tcp->dst_port = dst_port;
  tcp->seq_num = seq;
  tcp->ack_num = ack;

  tcp->checksum =
      bess::utils::CalculateIpv4TcpChecksum(*tcp, src_ip, dst_ip, sizeof(*tcp));
  ip->checksum = bess::utils::CalculateIpv4NoOptChecksum(*ip);

  return pkt;
}

CommandResponse UrlFilter::Init(const bess::pb::UrlFilterArg &arg) {
  for (const auto &url : arg.blacklist()) {
    if (blacklist_.find(url.host()) == blacklist_.end()) {
      blacklist_.emplace(std::piecewise_construct,
                         std::forward_as_tuple(url.host()),
                         std::forward_as_tuple());
    }
    Trie &trie = blacklist_.at(url.host());
    trie.Insert(url.path());
  }
  return CommandSuccess();
}

CommandResponse UrlFilter::CommandAdd(const bess::pb::UrlFilterArg &arg) {
  Init(arg);
  return CommandSuccess();
}

CommandResponse UrlFilter::CommandClear(const bess::pb::EmptyArg &) {
  blacklist_.clear();
  return CommandResponse();
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

    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

    if (ip->protocol != Ipv4::Proto::kTcp) {
      out_batches[0].add(pkt);
      continue;
    }

    int ip_bytes = ip->header_length << 2;
    Tcp *tcp =
        reinterpret_cast<Tcp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);

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
      if (tcp->flags & Tcp::Flag::kFin) {
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
          tcp->src_port, be32_t(tcp->ack_num.value() + strlen(HTTP_403_BODY)),
          tcp->seq_num));

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
