#ifndef BESS_CORE_UTILS_PCAP_HANDLE_H_
#define BESS_CORE_UTILS_PCAP_HANDLE_H_

#include "../common.h"
#include "pcap.h"
#include <pcap/pcap.h>
#include <string>

// Wraps all accesses to libpcap
class PcapHandle {
  friend class PcapHandleFixtureTest;

 public:
  // Opens a device and sets it to nonblocking
  PcapHandle(const std::string &dev);

  // Creates an empty PcapHandle that doesn't do anything.
  PcapHandle() : handle_(nullptr){};

  // Makes sure to close the connection before deleting.
  virtual ~PcapHandle();

  // Move constructor passes handle/ownership to new instance and zeroes out the
  // local copy.
  PcapHandle(PcapHandle &&other);

  // Move assignment operator clears out local state (and potentially releases
  // pcap
  // handle) and copies in handle from other; clears out other.
  PcapHandle &operator=(PcapHandle &&other);

  // Closes connection and sets to uninitialized state.
  void Reset();

  // Sends packet, returns 0 if success
  int SendPacket(const u_char *pkt, int len);

  // Receives a packet (returns ptr to that packet, stores capture length in to
  // caplen).
  const u_char *RecvPacket(int *caplen);

  // Is false if there's no pcap binding established
  bool is_initialized() const;

 private:
  // Only one instance of PcapHandle should exist for a given handle at a time.
  DISALLOW_COPY_AND_ASSIGN(PcapHandle);

  pcap_t *handle_;
};

#endif  // BESS_CORE_UTILS_PCAP_HANDLE_H_
