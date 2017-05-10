#ifndef BESS_DRIVERS_ZERO_COPY_VPORT_
#define BESS_DRIVERS_ZERO_COPY_VPORT_
#include <gtest/gtest.h>

#include "../kmod/llring.h"
#include "../message.h"
#include "../port.h"

#define SLOTS_PER_LLRING 1024

/* This watermark is to detect congestion and cache bouncing due to
 * head-eating-tail (needs at least 8 slots less then the total ring slots).
 * Not sure how to tune this... */
#define SLOTS_WATERMARK ((SLOTS_PER_LLRING >> 3) * 7) /* 87.5% */

/* Disable (0) single producer/consumer mode for now.
 * This is slower, but just to be on the safe side. :) */
#define SINGLE_P 0
#define SINGLE_C 0

#define PORT_NAME_LEN 128

#define VPORT_DIR_PREFIX "sn_vports"

struct vport_inc_regs {
  uint64_t dropped;
} __cacheline_aligned;

struct vport_out_regs {
  uint32_t irq_enabled;
} __cacheline_aligned;

/* This is equivalent to the old bar */
struct vport_bar {
  char name[PORT_NAME_LEN];

  /* The term RX/TX could be very confusing for a virtual switch.
   * Instead, we use the "incoming/outgoing" convention:
   * - incoming: outside -> BESS
   * - outgoing: BESS -> outside */
  int num_inc_q;
  int num_out_q;

  struct vport_inc_regs *inc_regs[MAX_QUEUES_PER_DIR];
  struct llring *inc_qs[MAX_QUEUES_PER_DIR];

  struct vport_out_regs *out_regs[MAX_QUEUES_PER_DIR];
  struct llring *out_qs[MAX_QUEUES_PER_DIR];
};

class ZeroCopyVPort final : public Port {
 public:
  CommandResponse Init(const bess::pb::EmptyArg &arg);

  void DeInit() override;

  int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) override;
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

 private:
  friend class ZeroCopyVPortTest;
  FRIEND_TEST(ZeroCopyVPortTest, Recv);

  struct vport_bar *bar_ = {};

  struct vport_inc_regs *inc_regs_[MAX_QUEUES_PER_DIR] = {};
  struct llring *inc_qs_[MAX_QUEUES_PER_DIR] = {};

  struct vport_out_regs *out_regs_[MAX_QUEUES_PER_DIR] = {};
  struct llring *out_qs_[MAX_QUEUES_PER_DIR] = {};

  int out_irq_fd_[MAX_QUEUES_PER_DIR] = {};
};

#endif  // BESS_DRIVERS_ZERO_COPY_VPORT_
