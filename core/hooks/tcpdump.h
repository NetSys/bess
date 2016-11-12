#ifndef BESS_HOOKS_TCPDUMP_
#define BESS_HOOKS_TCPDUMP_

#include "../module.h"

const std::string kGateHookTcpDumpGate = "tcpdump";

const uint16_t kGateHookPriorityTcpDump = 1;

// TcpDump dumps copies of the packets seen by a gate. Useful for debugging.
class TcpDump : public GateHook {
 public:
  TcpDump()
      : GateHook(kGateHookTcpDumpGate, kGateHookPriorityTcpDump), fifo_fd_(){};

  int fifo_fd() const { return fifo_fd_; }
  void set_fifo_fd(int fifo_fd) { fifo_fd_ = fifo_fd; }

  void ProcessBatch(const struct pkt_batch *batch);

 private:
  int fifo_fd_;
};

#endif  // BESS_HOOKS_TCPDUMP_
