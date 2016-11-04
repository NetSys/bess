#ifndef PCAP_PORT_H
#define PCAP_PORT_H

#include "../port.h"
#include "../utils/pcap_handle.h"
#include <glog/logging.h>

// Port to connect to a device via PCAP.
// (Not recommended because PCAP is slow :-)
// This driver is experimental. Currently does not support mbuf chaining and
// needs more tests!
class PCAPPort : public Port {
 public:
  PCAPPort(){};
  ~PCAPPort();

  virtual pb_error_t Init(const google::protobuf::Any &arg);

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
#endif
