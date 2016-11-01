#include "pcap_handle.h"

PcapHandle::PcapHandle(const std::string& dev) {
  if (dev.length() == 0) {
    return;
  }

  char errbuf[PCAP_ERRBUF_SIZE];
  handle_ = pcap_open_live(dev.c_str(), PCAP_SNAPLEN, 1, -1, errbuf);

  if (handle_ == nullptr) {
    return;
  }

  int ret = pcap_setnonblock(handle_, 1, errbuf);
  if (ret != 0) {
    pcap_close(handle_);
    handle_ = nullptr;
    return;
  }
}

PcapHandle::~PcapHandle() {
  Reset();
}

PcapHandle::PcapHandle(PcapHandle&& other) {
  handle_ = other.handle_;
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

void PcapHandle::Reset() {
  if (is_initialized()) {
    pcap_close(handle_);
    handle_ = nullptr;
  }
}

bool PcapHandle::is_initialized() const {
  return (handle_ != nullptr);
}

int PcapHandle::SendPacket(const u_char* packet, int len) {
  if (len <= PCAP_SNAPLEN) {
    return pcap_sendpacket(handle_, packet, len);
  } else {
    return 1;
  }
}

const u_char* PcapHandle::RecvPacket(int* caplen) {
  pcap_pkthdr header;
  const u_char* pkt = pcap_next(handle_, &header);
  *caplen = header.caplen;
  return pkt;
}
