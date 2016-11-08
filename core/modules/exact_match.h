#ifndef BESS_MODULES_EXACTMATCH_H_
#define BESS_MODULES_EXACTMATCH_H_

#include <rte_config.h>
#include <rte_hash_crc.h>

#include "../module.h"
#include "../module_msg.pb.h"
#include "../utils/htable.h"

#define MAX_FIELDS 8
#define MAX_FIELD_SIZE 8

static_assert(MAX_FIELD_SIZE <= sizeof(uint64_t),
              "field cannot be larger than 8 bytes");

#define HASH_KEY_SIZE (MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

using google::protobuf::RepeatedField;

struct em_hkey_t {
  uint64_t u64_arr[MAX_FIELDS];
};

inline int em_keycmp(const void *key, const void *key_stored, size_t key_len) {
  const uint64_t *a = ((em_hkey_t *)key)->u64_arr;
  const uint64_t *b = ((em_hkey_t *)key_stored)->u64_arr;

  switch (key_len >> 3) {
    default:
      promise_unreachable();
    case 8:
      if (unlikely(a[7] != b[7]))
        return 1;
    case 7:
      if (unlikely(a[6] != b[6]))
        return 1;
    case 6:
      if (unlikely(a[5] != b[5]))
        return 1;
    case 5:
      if (unlikely(a[4] != b[4]))
        return 1;
    case 4:
      if (unlikely(a[3] != b[3]))
        return 1;
    case 3:
      if (unlikely(a[2] != b[2]))
        return 1;
    case 2:
      if (unlikely(a[1] != b[1]))
        return 1;
    case 1:
      if (unlikely(a[0] != b[0]))
        return 1;
  }

  return 0;
}

inline uint32_t em_hash(const void *key, uint32_t key_len, uint32_t init_val) {
#if __SSE4_2__ && __x86_64
  const uint64_t *a = ((em_hkey_t *)key)->u64_arr;

  switch (key_len >> 3) {
    default:
      promise_unreachable();
    case 8:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 7:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 6:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 5:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 4:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 3:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 2:
      init_val = crc32c_sse42_u64(*a++, init_val);
    case 1:
      init_val = crc32c_sse42_u64(*a++, init_val);
  }

  return init_val;
#else
  return rte_hash_crc(key, key_len, init_val);
#endif
}

typedef HTable<em_hkey_t, gate_idx_t, em_keycmp, em_hash> htable_t;

struct EmField {
  /* bits with 1: the bit must be considered.
   * bits with 0: don't care */
  uint64_t mask;

  int attr_id; /* -1 for offset-based fields */

  /* Relative offset in the packet data for offset-based fields.
   *  (starts from data_off, not the beginning of the headroom */
  int offset;

  int pos; /* relative position in the key */

  int size; /* in bytes. 1 <= size <= MAX_FIELD_SIZE */
};

class ExactMatch : public Module {
 public:
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  ExactMatch()
      : Module(),
        default_gate_(),
        total_key_size_(),
        num_fields_(),
        fields_(),
        ht_() {}

  virtual struct snobj *Init(struct snobj *arg);

  virtual void Deinit();

  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual std::string GetDesc() const;
  virtual struct snobj *GetDump() const;

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandDelete(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);
  struct snobj *CommandSetDefaultGate(struct snobj *arg);

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

  pb_error_t InitPb(const bess::pb::ExactMatchArg &arg);
  bess::pb::ModuleCommandResponse CommandAddPb(
      const bess::pb::ExactMatchCommandAddArg &arg);
  bess::pb::ModuleCommandResponse CommandDeletePb(
      const bess::pb::ExactMatchCommandDeleteArg &arg);
  bess::pb::ModuleCommandResponse CommandClearPb(const bess::pb::EmptyArg &arg);
  bess::pb::ModuleCommandResponse CommandSetDefaultGatePb(
      const bess::pb::ExactMatchCommandSetDefaultGateArg &arg);

 private:
  struct snobj *AddFieldOne(struct snobj *field, struct EmField *f, int idx);
  struct snobj *GatherKey(struct snobj *fields, em_hkey_t *key);

  pb_error_t AddFieldOne(const bess::pb::ExactMatchArg_Field &field,
                         struct EmField *f, int idx);
  pb_error_t GatherKey(const RepeatedField<uint64_t> &fields, em_hkey_t *key);

  gate_idx_t default_gate_;

  uint32_t total_key_size_;

  int num_fields_;
  EmField fields_[MAX_FIELDS];

  htable_t ht_;
};

#endif  // BESS_MODULES_EXACTMATCH_H_
