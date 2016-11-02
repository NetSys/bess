#include "pcap.h"

PCAPPort::~PCAPPort() {
  if (pcap_handle_.IsInitialized()) {
    DeInit();
  }
}

// don't use
struct snobj *PCAPPort::Init(struct snobj *conf) {
  return nullptr;
}

pb_error_t PCAPPort::Init(const bess::protobuf::PCAPPortArg &arg) {
  if (pcap_handle_.IsInitialized()) {
    return pb_error(EINVAL, "Device already initialized.");
  }

  const std::string dev = arg.dev();
  pb_error_t err;
  pcap_handle_ = PCAPHandle(dev, &err);

  return err;
}

void PCAPPort::DeInit() {
  if (pcap_handle_.IsInitialized()) {
    pcap_handle_.~PCAPHandle();
  }
}

int PCAPPort::RecvPackets(queue_t qid, snb_array_t pkts, int cnt) {
  if (!pcap_handle_.IsInitialized()) {
    return 0;  // TODO: Would like to raise an error here...
  }

  return pcap_handle_.RecvPackets(pkts, cnt);
}

int PCAPPort::SendPackets(queue_t qid, snb_array_t pkts, int cnt) {
  if (!pcap_handle_.IsInitialized()) {
    return 0;  // TODO: Would like to raise an error here...
  }

  return pcap_handle_.SendPackets(pkts, cnt);
}

ADD_DRIVER(PCAPPort, "pcap_port", "libpcap live packet capture from Linux port")
