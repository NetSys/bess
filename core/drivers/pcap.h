#ifndef BESS_DRIVERS_PCAP_H_
#define BESS_DRIVERS_PCAP_H_

#include "../port.h"

#include <glog/logging.h>

#include "../utils/pcap_handle.h"

// Port to connect to a device via PCAP.
// (Not recommended because PCAP is slow :-)
// This driver is experimental. Currently does not support mbuf chaining and
// needs more tests!
class PCAPPort final : public Port {
 public:
  CommandResponse Init(const bess::pb::PCAPPortArg &arg);

  void DeInit() override;
  // PCAP has no notion of queue so unlike parent (port.cc) quid is ignored.
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;
  // Ditto above: quid is ignored.
  int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

 private:
  void GatherData(unsigned char *data, bess::Packet *pkt);
  PcapHandle pcap_handle_;
};

#endif  // BESS_DRIVERS_PCAP_H_
