#include "pcap_handle.h"

#include "pcap.h"

PcapHandle::PcapHandle(const std::string& dev) : handle_() {
  char errbuf[PCAP_ERRBUF_SIZE];
  handle_ = pcap_open_live(dev.c_str(), PCAP_SNAPLEN, 1, -1, errbuf);
}

PcapHandle::PcapHandle(pcap_t *handle) : handle_(handle) {}

PcapHandle::PcapHandle(PcapHandle&& other) : handle_(other.handle_) {
  other.handle_ = nullptr;
}

PcapHandle& PcapHandle::operator=(PcapHandle&& other) {
  if (this != &other) {
    if (is_initialized()) {
      Reset();
    }
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

PcapHandle::~PcapHandle() {
  Reset();
}

void PcapHandle::Reset() {
  if (is_initialized()) {
    pcap_close(handle_);
    handle_ = nullptr;
  }
}

int PcapHandle::SendPacket(const u_char* packet, int len) {
  if (is_initialized()) {
    return pcap_sendpacket(handle_, packet, len);
  } else {
    return -1;
  }
}

const u_char* PcapHandle::RecvPacket(int* caplen) {
  if (!is_initialized()) {
    *caplen = 0;
    return nullptr;
  }
  pcap_pkthdr header;
  const u_char* pkt = pcap_next(handle_, &header);
  *caplen = header.caplen;
  return pkt;
}

int PcapHandle::SetBlocking(bool block) {
  char errbuf[PCAP_ERRBUF_SIZE];
  return pcap_setnonblock(handle_, block ? 0 : 1, errbuf);
}
