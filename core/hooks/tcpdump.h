#ifndef BESS_HOOKS_TCPDUMP_
#define BESS_HOOKS_TCPDUMP_

#include "../module.h"

const std::string kGateHookTcpDumpGate = "tcpdump";

// TcpDump dumps copies of the packets seen by a gate. Useful for debugging.
class TcpDump : public GateHook {
 public:
  TcpDump(uint16_t priority = 0)
      : GateHook(kGateHookTcpDumpGate, priority),
        tcpdump_(),
        fifo_fd_(){};

  uint32_t tcpdump() const { return tcpdump_; }
  void set_tcpdump(uint32_t tcpdump) { tcpdump_ = tcpdump; }

  int fifo_fd() const { return fifo_fd_; }
  void set_fifo_fd(int fifo_fd) { fifo_fd_ = fifo_fd; }

  void ProcessBatch(struct pkt_batch *batch);

 private:
  uint32_t tcpdump_;
  int fifo_fd_;
};

#endif  // BESS_HOOKS_TCPDUMP_
