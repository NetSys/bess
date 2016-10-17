#include "../module.h"

#include <arpa/inet.h>

#include <rte_byteorder.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_lpm.h>

#define VECTOR_OPTIMIZATION 1

static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

class IpLookup : public Module {
 public:
  virtual struct snobj *Init(struct snobj *arg);
  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<IpLookup> cmds;

 private:
  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  struct rte_lpm *lpm_;
  gate_idx_t default_gate_;
};

const Commands<IpLookup> IpLookup::cmds = {
    {"add", &IpLookup::CommandAdd, 0},
    {"clear", &IpLookup::CommandClear, 0},
};

struct snobj *IpLookup::Init(struct snobj *arg) {
  struct rte_lpm_config conf = {
      .max_rules = 1024, .number_tbl8s = 128, .flags = 0,
  };

  default_gate_ = DROP_GATE;

  lpm_ = rte_lpm_create(Name().c_str(), /* socket_id = */ 0, &conf);

  if (!lpm_)
    return snobj_err(rte_errno, "DPDK error: %s", rte_strerror(rte_errno));

  return NULL;
}

void IpLookup::Deinit() { rte_lpm_free(lpm_); }

void IpLookup::ProcessBatch(struct pkt_batch *batch) {
  gate_idx_t ogates[MAX_PKT_BURST];
  gate_idx_t default_gate = default_gate_;

  int cnt = batch->cnt;
  int i;

#if VECTOR_OPTIMIZATION
  const __m128i bswap_mask =
      _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);

  /* 4 at a time */
  for (i = 0; i + 3 < cnt; i += 4) {
    struct ether_hdr *eth;
    struct ipv4_hdr *ip;

    uint32_t a0, a1, a2, a3;
    uint32_t next_hops[4];

    __m128i ip_addr;

    eth = (struct ether_hdr *)snb_head_data(batch->pkts[i + 0]);
    ip = (struct ipv4_hdr *)(eth + 1);
    a0 = ip->dst_addr;

    eth = (struct ether_hdr *)snb_head_data(batch->pkts[i + 1]);
    ip = (struct ipv4_hdr *)(eth + 1);
    a1 = ip->dst_addr;

    eth = (struct ether_hdr *)snb_head_data(batch->pkts[i + 2]);
    ip = (struct ipv4_hdr *)(eth + 1);
    a2 = ip->dst_addr;

    eth = (struct ether_hdr *)snb_head_data(batch->pkts[i + 3]);
    ip = (struct ipv4_hdr *)(eth + 1);
    a3 = ip->dst_addr;

    ip_addr = _mm_set_epi32(a3, a2, a1, a0);
    ip_addr = _mm_shuffle_epi8(ip_addr, bswap_mask);

    rte_lpm_lookupx4(lpm_, ip_addr, next_hops, default_gate);

    ogates[i + 0] = next_hops[0];
    ogates[i + 1] = next_hops[1];
    ogates[i + 2] = next_hops[2];
    ogates[i + 3] = next_hops[3];
  }
#endif

  /* process the rest one by one */
  for (; i < cnt; i++) {
    struct ether_hdr *eth;
    struct ipv4_hdr *ip;

    uint32_t next_hop;
    int ret;

    eth = (struct ether_hdr *)snb_head_data(batch->pkts[i]);
    ip = (struct ipv4_hdr *)(eth + 1);

    ret = rte_lpm_lookup(lpm_, rte_be_to_cpu_32(ip->dst_addr), &next_hop);

    if (ret == 0)
      ogates[i] = next_hop;
    else
      ogates[i] = default_gate;
  }

  run_split(this, ogates, batch);
}

struct snobj *IpLookup::CommandAdd(struct snobj *arg) {
  char *prefix = snobj_eval_str(arg, "prefix");
  uint32_t prefix_len = snobj_eval_uint(arg, "prefix_len");
  gate_idx_t gate = snobj_eval_uint(arg, "gate");

  struct in_addr ip_addr_be;
  uint32_t ip_addr; /* in cpu order */
  uint32_t netmask;
  int ret;

  if (!prefix || !snobj_eval_exists(arg, "prefix_len"))
    return snobj_err(EINVAL, "'prefix' or 'prefix_len' is missing");

  ret = inet_aton(prefix, &ip_addr_be);
  if (!ret) return snobj_err(EINVAL, "Invalid IP prefix: %s", prefix);

  if (prefix_len > 32)
    return snobj_err(EINVAL, "Invalid prefix length: %d", prefix_len);

  ip_addr = rte_be_to_cpu_32(ip_addr_be.s_addr);
  netmask = ~0 ^ ((1 << (32 - prefix_len)) - 1);

  if (ip_addr & ~netmask)
    return snobj_err(EINVAL, "Invalid IP prefix %s/%d %x %x", prefix,
                     prefix_len, ip_addr, netmask);

  if (!snobj_eval_exists(arg, "gate"))
    return snobj_err(EINVAL, "'gate' must be specified");

  if (!is_valid_gate(gate)) return snobj_err(EINVAL, "Invalid gate: %hu", gate);

  if (prefix_len == 0)
    default_gate_ = gate;
  else {
    ret = rte_lpm_add(lpm_, ip_addr, prefix_len, gate);
    if (ret) return snobj_err(-ret, "rpm_lpm_add() failed");
  }

  return NULL;
}

struct snobj *IpLookup::CommandClear(struct snobj *arg) {
  rte_lpm_delete_all(lpm_);
  default_gate_ = DROP_GATE;
  return NULL;
}

ADD_MODULE(IpLookup, "ip_lookup",
           "performs Longest Prefix Match on IPv4 packets")
