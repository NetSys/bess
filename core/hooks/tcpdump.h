#ifndef BESS_HOOKS_TCPDUMP_
#define BESS_HOOKS_TCPDUMP_

#include "../module.h"

const std::string kGateHookTcpDumpGate = "tcpdump";

// TcpDump dumps copies of the packets seen by a gate. Useful for debugging.
class TcpDump : public GateHook {
 public:
  TcpDump(uint16_t priority = 0)
      : GateHook(kGateHookTcpDumpGate, priority), enabled_(), fifo_fd_(){};

  bool enabled() const { return enabled_; }
  void enable() { enabled_ = true; }
  void disable() { enabled_ = false; }

  int fifo_fd() const { return fifo_fd_; }
  void set_fifo_fd(int fifo_fd) { fifo_fd_ = fifo_fd; }

  void ProcessBatch(const struct pkt_batch *batch);

 private:
  bool enabled_;
  int fifo_fd_;
};

#endif  // BESS_HOOKS_TCPDUMP_
