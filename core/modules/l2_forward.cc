// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "l2_forward.h"

#include <rte_hash_crc.h>

#include "../mem_alloc.h"
#include "../utils/endian.h"
#include "../utils/simd.h"

#define MAX_TABLE_SIZE (1048576 * 64)
#define DEFAULT_TABLE_SIZE 1024
#define MAX_BUCKET_SIZE 4

typedef uint64_t mac_addr_t;

static int is_power_of_2(uint64_t n) {
  return (n != 0 && ((n & (n - 1)) == 0));
}

/*
 * l2_init:
 *  Initilizes the l2_table.
 *  It creates the slots of MAX_TABLE_SIZE multiplied by MAX_BUCKET_SIZE.
 *
 * @l2tbl: pointer to
 * @size: number of hash value entries. must be power of 2, greater than 0, and
 *        less than equal to MAX_TABLE_SIZE (2^30)
 * @bucket: number of slots per hash value. must be power of 2, greater than 0,
 *        and less than equal to MAX_BUCKET_SIZE (4)
 */
static int l2_init(struct l2_table *l2tbl, int size, int bucket) {
  if (size <= 0 || size > MAX_TABLE_SIZE || !is_power_of_2(size)) {
    return -EINVAL;
  }

  if (bucket <= 0 || bucket > MAX_BUCKET_SIZE || !is_power_of_2(bucket)) {
    return -EINVAL;
  }

  if (l2tbl == nullptr) {
    return -EINVAL;
  }

  l2tbl->table = static_cast<l2_entry *>(mem_alloc_ex(
      sizeof(struct l2_entry) * size * bucket, alignof(struct l2_entry), 0));
  if (l2tbl->table == nullptr) {
    return -ENOMEM;
  }

  l2tbl->size = size;
  l2tbl->bucket = bucket;

  /* calculates the log_2 (size) */
  l2tbl->size_power = 0;
  while (size > 1) {
    size = size >> 1;
    l2tbl->size_power += 1;
  }

  return 0;
}

static int l2_deinit(struct l2_table *l2tbl) {
  if (l2tbl == nullptr || l2tbl->table == nullptr || l2tbl->size == 0 ||
      l2tbl->bucket == 0) {
    return -EINVAL;
  }

  mem_free(l2tbl->table);

  memset(l2tbl, 0, sizeof(struct l2_table));

  return 0;
}

static uint32_t l2_ib_to_offset(struct l2_table *l2tbl, int index, int bucket) {
  return index * l2tbl->bucket + bucket;
}

static uint32_t l2_hash(mac_addr_t addr) {
  return rte_hash_crc_8byte(addr, 0);
}

static uint32_t l2_hash_to_index(uint32_t hash, uint32_t size) {
  return hash & (size - 1);
}

static uint32_t l2_alt_index(uint32_t hash, uint32_t size_power,
                             uint32_t index) {
  uint64_t tag = (hash >> size_power) + 1;
  tag = tag * 0x5bd1e995;
  return (index ^ tag) & ((0x1lu << (size_power - 1)) - 1);
}

#if __AVX__
const union {
  uint64_t val[4];
  __m256d _mask;
} _mask = {.val = {0x8000ffffFFFFffffull, 0x8000ffffFFFFffffull,
                   0x8000ffffFFFFffffull, 0x8000ffffFFFFffffull}};

// Do not call these functions directly. Use find_index() instead. See below.
static inline int find_index_avx(uint64_t addr, uint64_t *table) {
  DCHECK(reinterpret_cast<uintptr_t>(table) % 32 == 0);
  __m256d _addr = (__m256d)_mm256_set1_epi64x(addr | (1ull << 63));
  __m256d _table = _mm256_load_pd((double *)table);
  _table = _mm256_and_pd(_table, _mask._mask);
  __m256d cmp = _mm256_cmp_pd(_addr, _table, _CMP_EQ_OQ);

  return __builtin_ffs(_mm256_movemask_pd(cmp));
}
#else
static inline int find_index_basic(uint64_t addr, uint64_t *table) {
  for (int i = 0; i < 4; i++) {
    if ((addr | (1ull << 63)) == (table[i] & 0x8000ffffFFFFffffull)) {
      return i + 1;
    }
  }

  return 0;
}
#endif

// Finds addr from a 4-way bucket *table and returns its index + 1.
// Returns zero if not found.
static inline int find_index(uint64_t addr, uint64_t *table, const uint64_t) {
#if __AVX__
  return find_index_avx(addr, table);
#else
  return find_index_basic(addr, table);
#endif
}

static inline int l2_find(struct l2_table *l2tbl, uint64_t addr,
                          gate_idx_t *gate) {
  size_t i;
  int ret = -ENOENT;
  uint32_t hash, idx1, offset;
  struct l2_entry *tbl = l2tbl->table;

  hash = l2_hash(addr);
  idx1 = l2_hash_to_index(hash, l2tbl->size);

  offset = l2_ib_to_offset(l2tbl, idx1, 0);

  if (l2tbl->bucket == 4) {
    int tmp1 = find_index(addr, &tbl[offset].entry, l2tbl->count);
    if (tmp1) {
      *gate = tbl[offset + tmp1 - 1].gate;
      return 0;
    }

    idx1 = l2_alt_index(hash, l2tbl->size_power, idx1);
    offset = l2_ib_to_offset(l2tbl, idx1, 0);

    int tmp2 = find_index(addr, &tbl[offset].entry, l2tbl->count);

    if (tmp2) {
      *gate = tbl[offset + tmp2 - 1].gate;
      return 0;
    }

  } else {
    /* search buckets for first index */
    for (i = 0; i < l2tbl->bucket; i++) {
      if (tbl[offset].occupied && addr == tbl[offset].addr) {
        *gate = tbl[offset].gate;
        return 0;
      }

      offset++;
    }

    idx1 = l2_alt_index(hash, l2tbl->size_power, idx1);
    offset = l2_ib_to_offset(l2tbl, idx1, 0);
    /* search buckets for alternate index */
    for (i = 0; i < l2tbl->bucket; i++) {
      if (tbl[offset].occupied && addr == tbl[offset].addr) {
        *gate = tbl[offset].gate;
        return 0;
      }

      offset++;
    }
  }

  return ret;
}

static int l2_find_offset(struct l2_table *l2tbl, uint64_t addr,
                          uint32_t *offset_out) {
  size_t i;
  uint32_t hash, idx1, offset;
  struct l2_entry *tbl = l2tbl->table;

  hash = l2_hash(addr);
  idx1 = l2_hash_to_index(hash, l2tbl->size);

  offset = l2_ib_to_offset(l2tbl, idx1, 0);
  /* search buckets for first index */
  for (i = 0; i < l2tbl->bucket; i++) {
    if (tbl[offset].occupied && addr == tbl[offset].addr) {
      *offset_out = offset;
      return 0;
    }

    offset++;
  }

  idx1 = l2_alt_index(hash, l2tbl->size_power, idx1);
  offset = l2_ib_to_offset(l2tbl, idx1, 0);
  /* search buckets for alternate index */
  for (i = 0; i < l2tbl->bucket; i++) {
    if (tbl[offset].occupied && addr == tbl[offset].addr) {
      *offset_out = offset;
      return 0;
    }

    offset++;
  }

  return -ENOENT;
}

static int l2_find_slot(struct l2_table *l2tbl, mac_addr_t addr, uint32_t *idx,
                        uint32_t *bucket) {
  size_t i, j;
  uint32_t hash;
  uint32_t idx1, idx_v1, idx_v2;
  uint32_t offset1, offset2;
  struct l2_entry *tbl = l2tbl->table;

  hash = l2_hash(addr);
  idx1 = l2_hash_to_index(hash, l2tbl->size);

  /* if there is available slot */
  for (i = 0; i < l2tbl->bucket; i++) {
    offset1 = l2_ib_to_offset(l2tbl, idx1, i);
    if (!tbl[offset1].occupied) {
      *idx = idx1;
      *bucket = i;
      return 0;
    }
  }

  offset1 = l2_ib_to_offset(l2tbl, idx1, 0);

  /* try moving */
  for (i = 0; i < l2tbl->bucket; i++) {
    offset1 = l2_ib_to_offset(l2tbl, idx1, i);
    hash = l2_hash(tbl[offset1].addr);
    idx_v1 = l2_hash_to_index(hash, l2tbl->size);
    idx_v2 = l2_alt_index(hash, l2tbl->size_power, idx_v1);

    /* if the alternate bucket is same as original skip it */
    if (idx_v1 == idx_v2 || idx1 == idx_v2)
      break;

    for (j = 0; j < l2tbl->bucket; j++) {
      offset2 = l2_ib_to_offset(l2tbl, idx_v2, j);
      if (!tbl[offset2].occupied) {
        /* move offset1 to offset2 */
        tbl[offset2] = tbl[offset1];
        /* clear offset1 */
        tbl[offset1].occupied = 0;

        *idx = idx1;
        *bucket = 0;
        return 0;
      }
    }
  }

  /* TODO:if alternate index is also full then start move */
  return -ENOMEM;
}

static int l2_add_entry(struct l2_table *l2tbl, mac_addr_t addr,
                        gate_idx_t gate) {
  uint32_t offset;
  uint32_t index;
  uint32_t bucket;
  gate_idx_t gate_idx_tmp;

  /* if addr already exist then fail */
  if (l2_find(l2tbl, addr, &gate_idx_tmp) == 0) {
    return -EEXIST;
  }

  /* find slots to put entry */
  if (l2_find_slot(l2tbl, addr, &index, &bucket) != 0) {
    return -ENOMEM;
  }

  /* insert entry into empty slot */
  offset = l2_ib_to_offset(l2tbl, index, bucket);

  l2tbl->table[offset].addr = addr;
  l2tbl->table[offset].gate = gate;
  l2tbl->table[offset].occupied = 1;
  l2tbl->count++;
  return 0;
}

static int l2_del_entry(struct l2_table *l2tbl, uint64_t addr) {
  uint32_t offset = 0xFFFFFFFF;

  if (l2_find_offset(l2tbl, addr, &offset)) {
    return -ENOENT;
  }

  l2tbl->table[offset].addr = 0;
  l2tbl->table[offset].gate = 0;
  l2tbl->table[offset].occupied = 0;
  l2tbl->count--;
  return 0;
}

static int l2_flush(struct l2_table *l2tbl) {
  if (nullptr == l2tbl || nullptr == l2tbl->table) {
    return -EINVAL;
  }

  memset(l2tbl->table, 0,
         sizeof(struct l2_entry) * l2tbl->size * l2tbl->bucket);

  return 0;
}

static uint64_t l2_addr_to_u64(char *addr) {
  uint64_t a = *(reinterpret_cast<uint32_t *>(addr));
  uint64_t b = *(reinterpret_cast<uint16_t *>(addr + 4));

  return a | (b << 32);
}

/******************************************************************************/
// TODO(barath): Move this test code elsewhere.

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void l2_forward_init_test() {
  int ret;
  struct l2_table l2tbl;

  ret = l2_init(&l2tbl, 0, 0);
  DCHECK_LT(ret, 0);

  ret = l2_init(&l2tbl, 4, 0);
  DCHECK_LT(ret, 0);

  ret = l2_init(&l2tbl, 0, 2);
  DCHECK_LT(ret, 0);

  ret = l2_init(&l2tbl, 4, 2);
  DCHECK(!ret);
  ret = l2_deinit(&l2tbl);
  DCHECK_EQ(ret, 0);

  ret = l2_init(&l2tbl, 4, 4);
  DCHECK(!ret);
  ret = l2_deinit(&l2tbl);
  DCHECK_EQ(ret, 0);

  ret = l2_init(&l2tbl, 4, 8);
  DCHECK_LT(ret, 0);

  ret = l2_init(&l2tbl, 6, 4);
  DCHECK_LT(ret, 0);

  ret = l2_init(&l2tbl, 2 << 10, 2);
  DCHECK_EQ(ret, 0);
  ret = l2_deinit(&l2tbl);
  DCHECK_EQ(ret, 0);

  ret = l2_init(&l2tbl, 2 << 10, 3);
  DCHECK_EQ(ret, 0);
}

static void l2_forward_entry_test() {
  int ret;
  struct l2_table l2tbl;

  uint64_t addr1 = 0x0123456701234567;
  uint64_t addr2 = 0x9876543210987654;
  uint16_t index1 = 0x0123;
  uint16_t gate_index = -1;

  ret = l2_init(&l2tbl, 4, 4);
  DCHECK_EQ(ret, 0);

  ret = l2_add_entry(&l2tbl, addr1, index1);
  LOG(INFO) << "add entry: " << addr1 << ", index: " << index1;
  DCHECK_EQ(ret, 0);

  ret = l2_find(&l2tbl, addr1, &gate_index);
  LOG(INFO) << "find entry: " << addr1 << ", index: " << gate_index;
  DCHECK_EQ(ret, 0);
  DCHECK_EQ(index1, gate_index);

  ret = l2_find(&l2tbl, addr2, &gate_index);
  DCHECK_LT(ret, 0);

  ret = l2_del_entry(&l2tbl, addr1);
  DCHECK_EQ(ret, 0);

  ret = l2_del_entry(&l2tbl, addr2);
  DCHECK_LT(ret, 0);

  ret = l2_find(&l2tbl, addr1, &gate_index);
  DCHECK_LT(ret, 0);

  ret = l2_deinit(&l2tbl);
  DCHECK_EQ(ret, 0);
}

static void l2_forward_flush_test() {
  int ret;
  struct l2_table l2tbl;

  uint64_t addr1 = 0x0123456701234567;
  uint16_t index1 = 0x0123;
  uint16_t gate_index;

  ret = l2_init(&l2tbl, 4, 4);
  DCHECK_EQ(ret, 0);

  ret = l2_add_entry(&l2tbl, addr1, index1);
  DCHECK_EQ(ret, 0);

  ret = l2_flush(&l2tbl);
  DCHECK_EQ(ret, 0);

  ret = l2_find(&l2tbl, addr1, &gate_index);
  DCHECK_LT(ret, 0);

  ret = l2_deinit(&l2tbl);
  DCHECK_EQ(ret, 0);
}

static void l2_forward_collision_test() {
  const int h_size = 4;
  const int b_size = 4;
  const int max_hb_cnt = h_size * b_size;

  int ret;
  int i;
  struct l2_table l2tbl;

  uint64_t addr[max_hb_cnt];
  uint16_t idx[max_hb_cnt];
  int success[max_hb_cnt];
  uint32_t offset;

  ret = l2_init(&l2tbl, h_size, b_size);
  DCHECK_EQ(ret, 0);

  /* collision happens */
  for (i = 0; i < max_hb_cnt; i++) {
    addr[i] = random() % ULONG_MAX;
    idx[i] = random() % USHRT_MAX;

    ret = l2_add_entry(&l2tbl, addr[i], idx[i]);
    LOG(INFO) << "insert result: " << addr[i] << " " << idx[i] << " " << ret;
    success[i] = (ret >= 0);
  }

  /* collision happens */
  for (i = 0; i < max_hb_cnt; i++) {
    uint16_t gate_index;
    gate_index = 0;
    offset = 0;

    ret = l2_find(&l2tbl, addr[i], &gate_index);

    LOG(INFO) << "find result: " << addr[i] << " " << gate_index << " "
              << offset;

    if (success[i]) {
      DCHECK_EQ(ret, 0);
      DCHECK_EQ(idx[i], gate_index);
    } else {
      DCHECK_NE(ret, 0);
    }
  }

  ret = l2_deinit(&l2tbl);
  DCHECK_EQ(ret, 0);
}

int test_all() {
  l2_forward_init_test();
  l2_forward_entry_test();
  l2_forward_flush_test();
  l2_forward_collision_test();

  return 0;
}

static int parse_mac_addr(const char *str, char *addr) {
  if (str != nullptr && addr != nullptr) {
    int r = sscanf(str, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", addr, addr + 1,
                   addr + 2, addr + 3, addr + 4, addr + 5);

    if (r != 6) {
      return -EINVAL;
    }
  }

  return 0;
}

/******************************************************************************/

const Commands L2Forward::cmds = {
    {"add", "L2ForwardCommandAddArg", MODULE_CMD_FUNC(&L2Forward::CommandAdd),
     Command::THREAD_UNSAFE},
    {"delete", "L2ForwardCommandDeleteArg",
     MODULE_CMD_FUNC(&L2Forward::CommandDelete), Command::THREAD_UNSAFE},
    {"set_default_gate", "L2ForwardCommandSetDefaultGateArg",
     MODULE_CMD_FUNC(&L2Forward::CommandSetDefaultGate), Command::THREAD_SAFE},
    {"lookup", "L2ForwardCommandLookupArg",
     MODULE_CMD_FUNC(&L2Forward::CommandLookup), Command::THREAD_SAFE},
    {"populate", "L2ForwardCommandPopulateArg",
     MODULE_CMD_FUNC(&L2Forward::CommandPopulate), Command::THREAD_UNSAFE},
};

CommandResponse L2Forward::Init(const bess::pb::L2ForwardArg &arg) {
  int ret = 0;
  int size = arg.size();
  int bucket = arg.bucket();

  default_gate_ = DROP_GATE;

  if (size == 0) {
    size = DEFAULT_TABLE_SIZE;
  }
  if (bucket == 0) {
    bucket = MAX_BUCKET_SIZE;
  }

  ret = l2_init(&l2_table_, size, bucket);

  if (ret != 0) {
    return CommandFailure(-ret,
                          "initialization failed with argument "
                          "size: '%d' bucket: '%d'",
                          size, bucket);
  }

  return CommandSuccess();
}

void L2Forward::DeInit() {
  l2_deinit(&l2_table_);
}

void L2Forward::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  gate_idx_t default_gate = ACCESS_ONCE(default_gate_);

  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *snb = batch->pkts()[i];

    gate_idx_t out_gate;
    // read destination MAC address (first 6 bytes)
    // NOTE: assumes little endian
    int ret = l2_find(&l2_table_,
                      *(snb->head_data<uint64_t *>()) & 0x0000ffffffffffff,
                      &out_gate);
    if (ret != 0) {
      EmitPacket(ctx, snb, default_gate);
    } else {
      EmitPacket(ctx, snb, out_gate);
    }
  }
}

CommandResponse L2Forward::CommandAdd(
    const bess::pb::L2ForwardCommandAddArg &arg) {
  for (int i = 0; i < arg.entries_size(); i++) {
    const auto &entry = arg.entries(i);

    if (!entry.addr().length()) {
      return CommandFailure(EINVAL,
                            "add list item map must contain addr as a string");
    }

    const char *str_addr = entry.addr().c_str();
    int gate = entry.gate();
    char addr[6];

    if (parse_mac_addr(str_addr, addr) != 0) {
      return CommandFailure(EINVAL, "%s is not a proper mac address", str_addr);
    }

    int r = l2_add_entry(&l2_table_, l2_addr_to_u64(addr), gate);

    if (r == -EEXIST) {
      return CommandFailure(EEXIST, "MAC address '%s' already exist", str_addr);
    } else if (r == -ENOMEM) {
      return CommandFailure(ENOMEM, "Not enough space");
    } else if (r != 0) {
      return CommandFailure(-r);
    }
  }

  return CommandSuccess();
}

CommandResponse L2Forward::CommandDelete(
    const bess::pb::L2ForwardCommandDeleteArg &arg) {
  for (int i = 0; i < arg.addrs_size(); i++) {
    const auto &_addr = arg.addrs(i);

    if (!_addr.length()) {
      return CommandFailure(EINVAL, "lookup must be list of string");
    }

    const char *str_addr = _addr.c_str();
    char addr[6];

    if (parse_mac_addr(str_addr, addr) != 0) {
      return CommandFailure(EINVAL, "%s is not a proper mac address", str_addr);
    }

    int r = l2_del_entry(&l2_table_, l2_addr_to_u64(addr));

    if (r == -ENOENT) {
      return CommandFailure(ENOENT, "MAC address '%s' does not exist",
                            str_addr);
    } else if (r != 0) {
      return CommandFailure(EINVAL, "Unknown Error: %d\n", r);
    }
  }

  return CommandSuccess();
}

CommandResponse L2Forward::CommandSetDefaultGate(
    const bess::pb::L2ForwardCommandSetDefaultGateArg &arg) {
  default_gate_ = arg.gate();
  return CommandSuccess();
}

CommandResponse L2Forward::CommandLookup(
    const bess::pb::L2ForwardCommandLookupArg &arg) {
  bess::pb::L2ForwardCommandLookupResponse ret;
  for (int i = 0; i < arg.addrs_size(); i++) {
    const auto &_addr = arg.addrs(i);

    if (!_addr.length()) {
      return CommandFailure(EINVAL, "lookup must be list of string");
    }

    const char *str_addr = _addr.c_str();
    char addr[6];

    if (parse_mac_addr(str_addr, addr) != 0) {
      return CommandFailure(EINVAL, "%s is not a proper mac address", str_addr);
    }

    gate_idx_t gate;
    int r = l2_find(&l2_table_, l2_addr_to_u64(addr), &gate);

    if (r == -ENOENT) {
      return CommandFailure(ENOENT, "MAC address '%s' does not exist",
                            str_addr);
    } else if (r != 0) {
      return CommandFailure(EINVAL, "Unknown Error: %d\n", r);
    }
    ret.add_gates(gate);
  }

  return CommandSuccess(ret);
}

CommandResponse L2Forward::CommandPopulate(
    const bess::pb::L2ForwardCommandPopulateArg &arg) {
  const char *base;
  char base_str[6] = {0};
  uint64_t base_u64;

  if (!arg.base().length()) {
    return CommandFailure(EINVAL, "base must exist in gen, and must be string");
  }

  // parse base addr
  base = arg.base().c_str();
  if (parse_mac_addr(base, base_str) != 0) {
    return CommandFailure(EINVAL, "%s is not a proper mac address", base_str);
  }

  base_u64 = l2_addr_to_u64(base_str);

  int cnt = arg.count();
  int gate_cnt = arg.gate_count();

  base_u64 = bess::utils::be64_t::swap(base_u64) >> 16;
  base_u64 = base_u64 >> 16;

  for (int i = 0; i < cnt; i++) {
    l2_add_entry(&l2_table_, bess::utils::be64_t::swap(base_u64 << 16),
                 i % gate_cnt);

    base_u64++;
  }

  return CommandSuccess();
}

ADD_MODULE(L2Forward, "l2_forward",
           "classifies packets with destination MAC address")
