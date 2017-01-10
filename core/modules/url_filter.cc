#include "url_filter.h"

#include <rte_ip.h>

#include <algorithm>

#include "../utils/ether.h"
#include "../utils/http_parser.h"
#include "../utils/ip.h"

using bess::utils::EthHeader;
using bess::utils::Ipv4Header;
using bess::utils::TcpHeader;

const uint64_t TIME_OUT = 12e10;

const Commands UrlFilter::cmds = {
    {"add", "UrlFilterArg", MODULE_CMD_FUNC(&UrlFilter::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&UrlFilter::CommandClear), 0}};

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
    ip.length = 20;
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
    tcp.flags = 0x14;
    tcp.window = 0;
    tcp.checksum = 0;  // To fill in
    tcp.urgent_ptr = 0;
  }
};

static PacketTemplate rst_template;

pb_error_t UrlFilter::Init(const bess::pb::UrlFilterArg &arg) {
  // XXX: Radix tree
  for (const auto &url : arg.blacklist()) {
    blacklist_.push_back(std::make_pair(url.host(), url.path()));
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

inline static bool IsStartsWith(const std::string &s,
                                const std::string &prefix) {
  return std::mismatch(prefix.begin(), prefix.end(), s.begin()).first ==
         prefix.end();
}

void UrlFilter::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t igate = get_igate();

  // Pass reverse traffic
  if (igate == 1) {
    RunChooseModule(1, batch);
    return;
  }

  // Otherwise
  bess::PacketBatch out_batches[2];
  out_batches[0].clear();
  out_batches[1].clear();
  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    struct EthHeader *eth = pkt->head_data<struct EthHeader *>();
    struct Ipv4Header *ip = reinterpret_cast<struct Ipv4Header *>(eth + 1);

    if (ip->protocol != 0x06) {
      continue;
    }

    int ip_bytes = (ip->header_length & 0xf) << 2;
    struct TcpHeader *tcp = reinterpret_cast<struct TcpHeader *>(
        reinterpret_cast<uint8_t *>(ip) + ip_bytes);

    Flow flow;
    flow.src_ip = ip->src;
    flow.dst_ip = ip->dst;
    flow.src_port = tcp->src_port;
    flow.src_port = tcp->dst_port;

    // Check if the flow is already blocked
    std::unordered_map<Flow, uint64_t, FlowHash>::iterator it;
    if ((it = blocked_flows_.find(flow)) != blocked_flows_.end()) {
      uint64_t now = tsc_to_ns(rdtsc());
      if (now - it->second < TIME_OUT) {
        bess::Packet::Free(pkt);
        continue;
      } else {
        blocked_flows_.erase(it);
      }
    }

    // Reconstruct this flow
    if (flow_cache_.find(flow) == flow_cache_.end()) {
      flow_cache_.emplace(std::piecewise_construct, std::make_tuple(flow),
                          std::make_tuple(1));
    }

    TcpFlowReconstruct &buffer = flow_cache_.at(flow);
    buffer.InsertPacket(pkt);

    struct phr_header headers[16];
    size_t num_headers = 16, method_len, path_len;
    int minor_version;
    const char *method, *path;
    int parse_result = phr_parse_request(
        buffer.buf(), buffer.contiguous_len(), &method, &method_len, &path,
        &path_len, &minor_version, headers, &num_headers, 0);

    bool matched = false;

    // -2 means incomplete
    if (parse_result > 0 || parse_result == -2) {
      const std::string path_str(path, path_len);

      // Look for the Host header
      for (size_t j = 0; j < num_headers; ++j) {
        if (strncmp(headers[j].name, "Host", headers[j].name_len) == 0) {
          const std::string host(headers[j].value, headers[j].value_len);

          // XXX: Use radix tree instead
          const auto url_it = std::find_if(
              blacklist_.begin(), blacklist_.end(),
              [&host](const Url &url) { return url.first == host; });

          if (url_it != blacklist_.end() &&
              IsStartsWith(path_str, url_it->second)) {
            // found a match
            matched = true;
            break;
          }
        }
      }
    }

    if (!matched) {
      out_batches[0].add(pkt);
    } else {
      blocked_flows_.emplace(flow, tsc_to_ns(rdtsc()));
      flow_cache_.erase(flow);

      // Drop the packet
      bess::Packet::Free(pkt);

      // XXX: Inject 403
      // Inject RST to destination
      {
        rst_template.eth.dst_addr = eth->dst_addr;
        rst_template.eth.src_addr = eth->src_addr;
        rst_template.ip.src = ip->src;
        rst_template.ip.dst = ip->dst;
        rst_template.tcp.src_port = tcp->src_port;
        rst_template.tcp.dst_port = tcp->dst_port;
        rst_template.tcp.seq_num = tcp->seq_num;
        rst_template.tcp.ack_num = tcp->ack_num;
        rst_template.tcp.checksum = rte_ipv4_udptcp_cksum(
            reinterpret_cast<const ipv4_hdr *>(&rst_template.ip),
            &rst_template.tcp);
        rst_template.ip.checksum = rte_ipv4_cksum(
            reinterpret_cast<const ipv4_hdr *>(&rst_template.ip));

        bess::Packet *rst = bess::Packet::Alloc();
        char *ptr = static_cast<char *>(rst->buffer()) + SNBUF_HEADROOM;
        rst->set_data_off(SNBUF_HEADROOM);
        rst->set_total_len(sizeof(rst_template));
        rst->set_data_len(sizeof(rst_template));
        rte_memcpy(ptr, &rst_template, sizeof(rst_template));
        out_batches[0].add(rst);
      }

      // Inject RST to source
      {
        rst_template.eth.dst_addr = eth->src_addr;
        rst_template.eth.src_addr = eth->dst_addr;
        rst_template.ip.src = ip->dst;
        rst_template.ip.dst = ip->src;
        rst_template.tcp.src_port = tcp->dst_port;
        rst_template.tcp.dst_port = tcp->src_port;
        rst_template.tcp.seq_num = tcp->ack_num;
        rst_template.tcp.ack_num = tcp->seq_num;
        rst_template.tcp.checksum = rte_ipv4_udptcp_cksum(
            reinterpret_cast<const ipv4_hdr *>(&rst_template.ip),
            &rst_template.tcp);
        rst_template.ip.checksum = rte_ipv4_cksum(
            reinterpret_cast<const ipv4_hdr *>(&rst_template.ip));

        bess::Packet *rst = bess::Packet::Alloc();
        char *ptr = static_cast<char *>(rst->buffer()) + SNBUF_HEADROOM;
        rst->set_data_off(SNBUF_HEADROOM);
        rst->set_total_len(sizeof(rst_template));
        rst->set_data_len(sizeof(rst_template));
        rte_memcpy(ptr, &rst_template, sizeof(rst_template));
        out_batches[1].add(rst);
      }
    }
  }

  RunChooseModule(0, &out_batches[0]);
  RunChooseModule(1, &out_batches[1]);
}

ADD_MODULE(UrlFilter, "url-filter", "Filter HTTP connection")
