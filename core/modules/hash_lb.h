#ifndef __HASH_LB_H__
#define __HASH_LB_H__

#include <gtest/gtest_prod.h>

#include "../module.h"

namespace bess {
namespace modules {

#define MAX_HLB_GATES 16384

/* TODO: add symmetric mode (e.g., LB_L4_SYM), v6 mode, etc. */
enum LbMode {
  LB_L2, /* dst MAC + src MAC */
  LB_L3, /* src IP + dst IP */
  LB_L4  /* L4 proto + src IP + dst IP + src port + dst port */
};

class HashLB : public Module {
 public:
  HashLB() : Module(), gates_(), num_gates_(), mode_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const bess::protobuf::HashLBArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandSetMode(struct snobj *arg);
  struct snobj *CommandSetGates(struct snobj *arg);

  pb_error_t CommandSetMode(const bess::protobuf::HashLBCommandSetModeArg &arg);
  pb_error_t CommandSetGates(
      const bess::protobuf::HashLBCommandSetGatesArg &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;

 private:
  FRIEND_TEST(HashLBTest, SetGatesVanilla);
  FRIEND_TEST(HashLBTest, SetGatesList);
  FRIEND_TEST(HashLBTest, SetMode);
  FRIEND_TEST(HashLBTest, Init);
  FRIEND_TEST(HashLBTest, InitWithMode);

  void LbL2(struct pkt_batch *batch, gate_idx_t *ogates);
  void LbL3(struct pkt_batch *batch, gate_idx_t *ogates);
  void LbL4(struct pkt_batch *batch, gate_idx_t *ogates);

  gate_idx_t gates_[MAX_HLB_GATES];
  int num_gates_;
  enum LbMode mode_;
};

} // namespace modules
} // namespace bess

#endif
