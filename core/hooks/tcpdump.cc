#include "tcpdump.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <glog/logging.h>

#include "../message.h"
#include "../utils/common.h"
#include "../utils/pcap.h"
#include "../utils/time.h"

const std::string Tcpdump::kName = "tcpdump";

Tcpdump::Tcpdump()
    : bess::GateHook(Tcpdump::kName, Tcpdump::kPriority), fifo_fd_() {}

Tcpdump::~Tcpdump() {
  close(fifo_fd_);
}

CommandResponse Tcpdump::Init(const bess::Gate *,
                              const bess::pb::TcpdumpArg &arg) {
  static const struct pcap_hdr PCAP_FILE_HDR = {
      .magic_number = PCAP_MAGIC_NUMBER,
      .version_major = PCAP_VERSION_MAJOR,
      .version_minor = PCAP_VERSION_MINOR,
      .thiszone = PCAP_THISZONE,
      .sigfigs = PCAP_SIGFIGS,
      .snaplen = PCAP_SNAPLEN,
      .network = PCAP_NETWORK,
  };
  int ret;

  fifo_fd_ = open(arg.fifo().c_str(), O_WRONLY | O_NONBLOCK);
  if (fifo_fd_ < 0) {
    return CommandFailure(-errno, "Failed to open FIFO");
  }

  ret = fcntl(fifo_fd_, F_SETFL, fcntl(fifo_fd_, F_GETFL) | O_NONBLOCK);
  if (ret < 0) {
    close(fifo_fd_);
    return CommandFailure(-errno, "fnctl() on FIFO failed");
  }

  ret = write(fifo_fd_, &PCAP_FILE_HDR, sizeof(PCAP_FILE_HDR));
  if (ret < 0) {
    close(fifo_fd_);
    return CommandFailure(-errno, "Failed to write PCAP header");
  }

  return CommandSuccess();
}

void Tcpdump::ProcessBatch(const bess::PacketBatch *batch) {
  struct timeval tv;

  int ret = 0;

  gettimeofday(&tv, nullptr);

  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *pkt = batch->pkts()[i];
    struct pcap_rec_hdr rec = {
        .ts_sec = (uint32_t)tv.tv_sec,
        .ts_usec = (uint32_t)tv.tv_usec,
        .incl_len = (uint32_t)pkt->head_len(),
        .orig_len = (uint32_t)pkt->total_len(),
    };

    struct iovec vec[2] = {{&rec, sizeof(rec)},
                           {pkt->head_data(), (size_t)pkt->total_len()}};

    ret = writev(fifo_fd_, vec, 2);
    if (ret < 0) {
      if (errno == EPIPE) {
        DLOG(WARNING) << "Broken pipe: stopping tcpdump";
        close(fifo_fd_);
        fifo_fd_ = 0;
      }
      return;
    }
  }
}

ADD_GATE_HOOK(Tcpdump)
