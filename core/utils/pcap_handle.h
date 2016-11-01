#ifndef PCAP_HANDLE_H
#define PCAP_HANDLE_H

#include <errno.h>
#include <pcap/pcap.h>

#include <glog/logging.h>

#include "../message.h"
#include "../port.h"
#include "../snobj.h"
#include "../utils/pcap.h"

// Wraps all accesses to libpcap; opens a device, sets it nonblocking, and then
// lets you send/recv packets.
class PCAPHandle {
 public:
  PCAPHandle(std::string dev, pb_error_t* error);
  PCAPHandle();

  ~PCAPHandle();

  int SendPackets(snb_array_t pkts, int cnt);

  int RecvPackets(snb_array_t pkts, int cnt);

  bool IsInitialized();

 private:
  // For chained mbufs, this function converts back to pcap packets with
  // contiguous data.
  void GatherData(unsigned char* tx_pcap_data, struct rte_mbuf* mbuf);

  bool is_initialized_;

  pcap_t* handle_;
};

#endif
