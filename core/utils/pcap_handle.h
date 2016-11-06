#ifndef BESS_UTILS_PCAP_HANDLE_H_
#define BESS_UTILS_PCAP_HANDLE_H_

#include <string>

#include <pcap/pcap.h>

#include "common.h"

// Wraps all accesses to libpcap
class PcapHandle {
 public:
  // Open a device and sets it to nonblocking
  PcapHandle(const std::string &dev);

  // Create PcapHandle with an existing PCAP handle.
  // handle can be nullptr, if you want to create an empty handle.
  PcapHandle(pcap_t *handle = nullptr);

  // Move constructor passes handle/ownership to new instance and zeroes out the
  // local copy.
  PcapHandle(PcapHandle &&other);

  // Move assignment operator clears out local state (and potentially releases
  // pcap handle) and copies in handle from other; clears out other.
  PcapHandle &operator=(PcapHandle &&other);

  // Make sure to close the connection before deleting.
  virtual ~PcapHandle();

  // Close connection and sets to uninitialized state.
  void Reset();

  // Send packet, returns 0 if success, -1 otherwise
  int SendPacket(const u_char *pkt, int len);

  // Receive a packet (returns ptr to that packet, stores capture length in to
  // caplen).
  // FIXME: the caller should provide a buffer, not the callee
  const u_char *RecvPacket(int *caplen);

  // Set blocking mode for live device capture. Retuen -1 if failed
  int SetBlocking(bool block);

  // Return false if there's no pcap binding established
  bool is_initialized() const { return (handle_ != nullptr); }

 private:
  pcap_t *handle_;

  // Only one instance of PcapHandle should exist for a given handle at a time.
  DISALLOW_COPY_AND_ASSIGN(PcapHandle);
};

#endif  // BESS_UTILS_PCAP_HANDLE_H_
