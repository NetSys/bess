#include "tcpdump.h"

#include <sys/uio.h>

#include <glog/logging.h>

#include "../utils/common.h"
#include "../utils/pcap.h"
#include "../utils/time.h"

void TcpDump::ProcessBatch(const struct bess::pkt_batch *batch) {
  struct timeval tv;

  int ret = 0;

  gettimeofday(&tv, nullptr);

  for (int i = 0; i < batch->cnt; i++) {
    bess::Packet *pkt = batch->pkts[i];
    struct pcap_rec_hdr rec = {
        .ts_sec = (uint32_t)tv.tv_sec,
        .ts_usec = (uint32_t)tv.tv_usec,
        .incl_len = pkt->mbuf().data_len,
        .orig_len = pkt->mbuf().pkt_len,
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
