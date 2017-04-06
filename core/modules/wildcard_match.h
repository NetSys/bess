#ifndef BESS_MODULES_WILDCARDMATCH_H_
#define BESS_MODULES_WILDCARDMATCH_H_

#include "../module.h"

#include <rte_config.h>
#include <rte_hash_crc.h>

#include "../module_msg.pb.h"
#include "../utils/cuckoo_map.h"

using bess::utils::HashResult;
using bess::utils::CuckooMap;

#define MAX_TUPLES 8
#define MAX_FIELDS 8
#define MAX_FIELD_SIZE 8
static_assert(MAX_FIELD_SIZE <= sizeof(uint64_t),
              "field cannot be larger than 8 bytes");

#define HASH_KEY_SIZE (MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

struct WmData {
  int priority;
  gate_idx_t ogate;
};

struct WmField {
  int attr_id; /* -1 for offset-based fields */

  /* Relative offset in the packet data for offset-based fields.
   *  (starts from data_off, not the beginning of the headroom */
  int offset;

  int pos; /* relative position in the key */

  int size; /* in bytes. 1 <= size <= MAX_FIELD_SIZE */
};

struct wm_hkey_t {
  uint64_t u64_arr[MAX_FIELDS];
};

class wm_eq {
 public:
  explicit wm_eq(size_t len) : len_(len) {}

  bool operator()(const wm_hkey_t &lhs, const wm_hkey_t &rhs) const {
    promise(len_ >= sizeof(uint64_t));
    promise(len_ <= sizeof(wm_hkey_t));

    for (size_t i = 0; i < len_ / 8; i++) {
// Disable uninitialized variable checking in GCC due to false positives
#if !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
      if (lhs.u64_arr[i] != rhs.u64_arr[i]) {
#if !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        return false;
      }
    }
    return true;
  }

 private:
  size_t len_;
};

class wm_hash {
 public:
  wm_hash() : len_(sizeof(wm_hkey_t)) {}
  explicit wm_hash(size_t len) : len_(len) {}

  HashResult operator()(const wm_hkey_t &key) const {
    HashResult init_val = 0;

    promise(len_ >= sizeof(uint64_t));
    promise(len_ <= sizeof(wm_hkey_t));

#if __SSE4_2__ && __x86_64
    for (size_t i = 0; i < len_ / 8; i++) {
      init_val = crc32c_sse42_u64(key.u64_arr[i], init_val);
    }
    return init_val;
#else
    return rte_hash_crc(&key, len_, init_val);
#endif
  }

 private:
  size_t len_;
};

class WildcardMatch final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;

  WildcardMatch()
      : Module(), default_gate_(), total_key_size_(), fields_(), tuples_() {}

  pb_error_t Init(const bess::pb::WildcardMatchArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

  pb_cmd_response_t CommandAdd(const bess::pb::WildcardMatchCommandAddArg &arg);
  pb_cmd_response_t CommandDelete(
      const bess::pb::WildcardMatchCommandDeleteArg &arg);
  pb_cmd_response_t CommandClear(const bess::pb::EmptyArg &arg);
  pb_cmd_response_t CommandSetDefaultGate(
      const bess::pb::WildcardMatchCommandSetDefaultGateArg &arg);

 private:
  struct WmTuple {
    CuckooMap<wm_hkey_t, struct WmData, wm_hash, wm_eq> ht;
    wm_hkey_t mask;
  };

  gate_idx_t LookupEntry(const wm_hkey_t &key, gate_idx_t def_gate);

  pb_error_t AddFieldOne(const bess::pb::WildcardMatchArg_Field &field,
                         struct WmField *f);

  template <typename T>
  pb_error_t ExtractKeyMask(const T &arg, wm_hkey_t *key, wm_hkey_t *mask);

  int FindTuple(wm_hkey_t *mask);
  int AddTuple(wm_hkey_t *mask);
  int DelEntry(int idx, wm_hkey_t *key);

  gate_idx_t default_gate_;

  size_t total_key_size_; /* a multiple of sizeof(uint64_t) */

  std::vector<struct WmField> fields_;
  std::vector<struct WmTuple> tuples_;
};

#endif  // BESS_MODULES_WILDCARDMATCH_H_
