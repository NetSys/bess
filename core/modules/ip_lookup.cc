#include "ip_lookup.h"

#include <rte_config.h>
#include <rte_errno.h>
#include <rte_lpm.h>

#include "../utils/ether.h"
#include "../utils/ip.h"

#define VECTOR_OPTIMIZATION 1

static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

const Commands IPLookup::cmds = {
    {"add", "IPLookupCommandAddArg", MODULE_CMD_FUNC(&IPLookup::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&IPLookup::CommandClear), 0}};

CommandResponse IPLookup::Init(const bess::pb::IPLookupArg &arg) {
  struct rte_lpm_config conf = {
      .max_rules = arg.max_rules() ? arg.max_rules() : 1024,
      .number_tbl8s = arg.max_tbl8s() ? arg.max_tbl8s() : 128,
      .flags = 0,
  };

  default_gate_ = DROP_GATE;

  lpm_ = rte_lpm_create(name().c_str(), /* socket_id = */ 0, &conf);

  if (!lpm_) {
    return CommandFailure(rte_errno, "DPDK error: %s", rte_strerror(rte_errno));
  }

  return CommandSuccess();
}

void IPLookup::DeInit() {
  if (lpm_) {
    rte_lpm_free(lpm_);
  }
}

void IPLookup::ProcessBatch(bess::PacketBatch *batch) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;

  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];
  gate_idx_t default_gate = default_gate_;

  int cnt = batch->cnt();
  int i;

#if VECTOR_OPTIMIZATION
  const __m128i bswap_mask =
      _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);

  /* 4 at a time */
  for (i = 0; i + 3 < cnt; i += 4) {
    Ethernet *eth;
    Ipv4 *ip;

    uint32_t a0, a1, a2, a3;
    uint32_t next_hops[4];

    __m128i ip_addr;

    eth = batch->pkts()[i]->head_data<Ethernet *>();
    ip = (Ipv4 *)(eth + 1);
    a0 = ip->dst.raw_value();

    eth = batch->pkts()[i + 1]->head_data<Ethernet *>();
    ip = (Ipv4 *)(eth + 1);
    a1 = ip->dst.raw_value();

    eth = batch->pkts()[i + 2]->head_data<Ethernet *>();
    ip = (Ipv4 *)(eth + 1);
    a2 = ip->dst.raw_value();

    eth = batch->pkts()[i + 3]->head_data<Ethernet *>();
    ip = (Ipv4 *)(eth + 1);
    a3 = ip->dst.raw_value();

    ip_addr = _mm_set_epi32(a3, a2, a1, a0);
    ip_addr = _mm_shuffle_epi8(ip_addr, bswap_mask);

    rte_lpm_lookupx4(lpm_, ip_addr, next_hops, default_gate);

    out_gates[i + 0] = next_hops[0];
    out_gates[i + 1] = next_hops[1];
    out_gates[i + 2] = next_hops[2];
    out_gates[i + 3] = next_hops[3];
  }
#endif

  /* process the rest one by one */
  for (; i < cnt; i++) {
    Ethernet *eth;
    Ipv4 *ip;

    uint32_t next_hop;
    int ret;

    eth = batch->pkts()[i]->head_data<Ethernet *>();
    ip = (Ipv4 *)(eth + 1);

    ret = rte_lpm_lookup(lpm_, ip->dst.raw_value(), &next_hop);

    if (ret == 0) {
      out_gates[i] = next_hop;
    } else {
      out_gates[i] = default_gate;
    }
  }

  RunSplit(out_gates, batch);
}

CommandResponse IPLookup::CommandAdd(
    const bess::pb::IPLookupCommandAddArg &arg) {
  using bess::utils::be32_t;

  be32_t net_addr;
  be32_t net_mask;
  gate_idx_t gate = arg.gate();

  if (!arg.prefix().length()) {
    return CommandFailure(EINVAL, "prefix' is missing");
  }
  if (!bess::utils::ParseIpv4Address(arg.prefix(), &net_addr)) {
    return CommandFailure(EINVAL, "Invalid IP prefix: %s",
                          arg.prefix().c_str());
  }

  uint64_t prefix_len = arg.prefix_len();
  if (prefix_len > 32) {
    return CommandFailure(EINVAL, "Invalid prefix length: %" PRIu64,
                          prefix_len);
  }

  net_mask = be32_t(~((1ull << (32 - prefix_len)) - 1));

  if ((net_addr & ~net_mask).value()) {
    return CommandFailure(EINVAL, "Invalid IP prefix %s/%" PRIu64 " %x %x",
                          arg.prefix().c_str(), prefix_len, net_addr.value(),
                          net_mask.value());
  }

  if (!is_valid_gate(gate)) {
    return CommandFailure(EINVAL, "Invalid gate: %hu", gate);
  }

  if (prefix_len == 0) {
    default_gate_ = gate;
  } else {
    int ret = rte_lpm_add(lpm_, net_addr.value(), prefix_len, gate);
    if (ret) {
      return CommandFailure(-ret, "rpm_lpm_add() failed");
    }
  }

  return CommandSuccess();
}

CommandResponse IPLookup::CommandClear(const bess::pb::EmptyArg &) {
  rte_lpm_delete_all(lpm_);
  return CommandSuccess();
}

ADD_MODULE(IPLookup, "ip_lookup",
           "performs Longest Prefix Match on IPv4 packets")
