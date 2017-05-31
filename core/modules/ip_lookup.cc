#include "ip_lookup.h"

#include <rte_config.h>
#include <rte_errno.h>
#include <rte_lpm.h>

#include "../utils/ether.h"
#include "../utils/ip.h"

#define VECTOR_OPTIMIZATION 1

// Strangely, rte_lpm_lookupx4() in rte_lpm_sse.h causes a build error
// when g++ is run with -O0 option.
// error: the last argument must be an 8-bit immediate
//   i24 = _mm_srli_si128(i24, sizeof(uint64_t));
// ... although the last argument is indeed a compile-time constant.
// To work around the issue as a quick and dirty fix, this function was copied
// from DPDK 17.02 to modify its body. See #if 0 .. #endif block below.
#ifndef __OPTIMIZE__
static void lpm_lookupx4(const struct rte_lpm *lpm, __m128i ip, uint32_t hop[4],
                         uint32_t defv) {
  __m128i i24;
  rte_xmm_t i8;
  uint32_t tbl[4];
  uint64_t idx, pt, pt2;
  const uint32_t *ptbl;

  const __m128i mask8 =
      _mm_set_epi32(UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX);

  /*
   * RTE_LPM_VALID_EXT_ENTRY_BITMASK for 2 LPM entries
   * as one 64-bit value (0x0300000003000000).
   */
  const uint64_t mask_xv = ((uint64_t)RTE_LPM_VALID_EXT_ENTRY_BITMASK |
                            (uint64_t)RTE_LPM_VALID_EXT_ENTRY_BITMASK << 32);

  /*
   * RTE_LPM_LOOKUP_SUCCESS for 2 LPM entries
   * as one 64-bit value (0x0100000001000000).
   */
  const uint64_t mask_v = ((uint64_t)RTE_LPM_LOOKUP_SUCCESS |
                           (uint64_t)RTE_LPM_LOOKUP_SUCCESS << 32);

  /* get 4 indexes for tbl24[]. */
  i24 = _mm_srli_epi32(ip, CHAR_BIT);

  /* extract values from tbl24[] */
  idx = _mm_cvtsi128_si64(i24);
#if 0
	i24 = _mm_srli_si128(i24, sizeof(uint64_t));
#else
  i24 = _mm_srli_si128(i24, 8);
#endif
  ptbl = (const uint32_t *)&lpm->tbl24[(uint32_t)idx];
  tbl[0] = *ptbl;
  ptbl = (const uint32_t *)&lpm->tbl24[idx >> 32];
  tbl[1] = *ptbl;

  idx = _mm_cvtsi128_si64(i24);

  ptbl = (const uint32_t *)&lpm->tbl24[(uint32_t)idx];
  tbl[2] = *ptbl;
  ptbl = (const uint32_t *)&lpm->tbl24[idx >> 32];
  tbl[3] = *ptbl;

  /* get 4 indexes for tbl8[]. */
  i8.x = _mm_and_si128(ip, mask8);

  pt = (uint64_t)tbl[0] | (uint64_t)tbl[1] << 32;
  pt2 = (uint64_t)tbl[2] | (uint64_t)tbl[3] << 32;

  /* search successfully finished for all 4 IP addresses. */
  if (likely((pt & mask_xv) == mask_v) && likely((pt2 & mask_xv) == mask_v)) {
    *(uint64_t *)hop = pt & RTE_LPM_MASKX4_RES;
    *(uint64_t *)(hop + 2) = pt2 & RTE_LPM_MASKX4_RES;
    return;
  }

  if (unlikely((pt & RTE_LPM_VALID_EXT_ENTRY_BITMASK) ==
               RTE_LPM_VALID_EXT_ENTRY_BITMASK)) {
    i8.u32[0] = i8.u32[0] + (uint8_t)tbl[0] * RTE_LPM_TBL8_GROUP_NUM_ENTRIES;
    ptbl = (const uint32_t *)&lpm->tbl8[i8.u32[0]];
    tbl[0] = *ptbl;
  }
  if (unlikely((pt >> 32 & RTE_LPM_VALID_EXT_ENTRY_BITMASK) ==
               RTE_LPM_VALID_EXT_ENTRY_BITMASK)) {
    i8.u32[1] = i8.u32[1] + (uint8_t)tbl[1] * RTE_LPM_TBL8_GROUP_NUM_ENTRIES;
    ptbl = (const uint32_t *)&lpm->tbl8[i8.u32[1]];
    tbl[1] = *ptbl;
  }
  if (unlikely((pt2 & RTE_LPM_VALID_EXT_ENTRY_BITMASK) ==
               RTE_LPM_VALID_EXT_ENTRY_BITMASK)) {
    i8.u32[2] = i8.u32[2] + (uint8_t)tbl[2] * RTE_LPM_TBL8_GROUP_NUM_ENTRIES;
    ptbl = (const uint32_t *)&lpm->tbl8[i8.u32[2]];
    tbl[2] = *ptbl;
  }
  if (unlikely((pt2 >> 32 & RTE_LPM_VALID_EXT_ENTRY_BITMASK) ==
               RTE_LPM_VALID_EXT_ENTRY_BITMASK)) {
    i8.u32[3] = i8.u32[3] + (uint8_t)tbl[3] * RTE_LPM_TBL8_GROUP_NUM_ENTRIES;
    ptbl = (const uint32_t *)&lpm->tbl8[i8.u32[3]];
    tbl[3] = *ptbl;
  }

  hop[0] = (tbl[0] & RTE_LPM_LOOKUP_SUCCESS) ? tbl[0] & 0x00FFFFFF : defv;
  hop[1] = (tbl[1] & RTE_LPM_LOOKUP_SUCCESS) ? tbl[1] & 0x00FFFFFF : defv;
  hop[2] = (tbl[2] & RTE_LPM_LOOKUP_SUCCESS) ? tbl[2] & 0x00FFFFFF : defv;
  hop[3] = (tbl[3] & RTE_LPM_LOOKUP_SUCCESS) ? tbl[3] & 0x00FFFFFF : defv;
}

// Substitute DPDK's function with ours.
#define rte_lpm_lookupx4 lpm_lookupx4
#endif

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
