#ifndef BESS_DRIVERS_PCAP_H_
#define BESS_DRIVERS_PCAP_H_

#include "../port.h"

#include <glog/logging.h>

#include "../utils/pcap_handle.h"

// Port to connect to a device via PCAP.
// (Not recommended because PCAP is slow :-)
// This driver is experimental. Currently does not support mbuf chaining and
// needs more tests!
class PCAPPort : public Port {
 public:
  pb_error_t InitPb(const bess::pb::PCAPPortArg &arg);

  // DEPRECATED DONT USE
  virtual struct snobj *Init(struct snobj *arg);

  virtual void DeInit();
  // PCAP has no notion of queue so unlike parent (port.cc) quid is ignored.
  virtual int SendPackets(queue_t qid, snb_array_t pkts, int cnt);
  // Ditto above: quid is ignored.
  virtual int RecvPackets(queue_t qid, snb_array_t pkts, int cnt);

 private:
  void GatherData(unsigned char *data, struct rte_mbuf *mbuf);
  PcapHandle pcap_handle_;
};

#endif  // BESS_DRIVERS_PCAP_H_
