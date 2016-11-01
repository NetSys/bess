#include "pcap_handle.h"

PCAPHandle::PCAPHandle() {
  is_initialized_ = 0;
  handle_ = nullptr;
}

PCAPHandle::PCAPHandle(std::string dev, pb_error_t* error) {
  if (dev.length() == 0) {
    *error = pb_error(EINVAL, "PCAP requires device name.");
    return;
  }

  char errbuf[PCAP_ERRBUF_SIZE];
  handle_ = pcap_open_live(dev.c_str(), PCAP_SNAPLEN, 1, -1, errbuf);

  if (handle_ == nullptr) {
    *error = pb_error(ENODEV, "PCAP Open dev error: %s", errbuf);
    handle_ = 0;
    return;
  }

  int ret = pcap_setnonblock(handle_, 1, errbuf);
  if (ret != 0) {
    pcap_close(handle_);
    *error = pb_error(ENODEV, "PCAP set to nonblock error: %s", errbuf);
    return;
  }

  is_initialized_ = true;
  LOG(INFO) << "PCAP: open dev " << dev;
}

PCAPHandle::~PCAPHandle() {
  if (is_initialized_) {
    pcap_close(handle_);
    handle_ = nullptr;
    is_initialized_ = false;
  }
}

bool PCAPHandle::IsInitialized() {
  return is_initialized_;
}

int PCAPHandle::SendPackets(snb_array_t pkts, int cnt) {
  int ret;
  int send_cnt = 0;

  while (send_cnt < cnt) {
    struct snbuf* sbuf = pkts[send_cnt];

    if (likely(sbuf->mbuf.nb_segs == 1)) {
      ret = pcap_sendpacket(handle_, (const u_char*)snb_head_data(sbuf),
                            sbuf->mbuf.pkt_len);
    } else if (sbuf->mbuf.pkt_len <= PCAP_SNAPLEN) {
      unsigned char tx_pcap_data[PCAP_SNAPLEN];
      GatherData(tx_pcap_data, &sbuf->mbuf);
      ret = pcap_sendpacket(handle_, tx_pcap_data, sbuf->mbuf.pkt_len);
    } else {
      RTE_LOG(ERR, PMD, "PCAP Packet Drop. Size (%d) > max size (%d).\n",
              sbuf->mbuf.pkt_len, PCAP_SNAPLEN);
      break;
    }

    if (unlikely(ret != 0)) {
      break;
    }

    send_cnt++;
  }

  snb_free_bulk(pkts, send_cnt);
  return send_cnt;
}

void PCAPHandle::GatherData(unsigned char* data, struct rte_mbuf* mbuf) {
  uint16_t data_len = 0;

  while (mbuf) {
    rte_memcpy(data + data_len, rte_pktmbuf_mtod(mbuf, void*), mbuf->data_len);

    data_len += mbuf->data_len;
    mbuf = mbuf->next;
  }
}

int PCAPHandle::RecvPackets(snb_array_t pkts, int cnt) {
  const u_char* packet;
  struct pcap_pkthdr header;

  int recv_cnt = 0;
  struct snbuf* sbuf;

  while (recv_cnt < cnt) {
    packet = pcap_next(handle_, &header);
    if (!packet) {
      break;
    }

    sbuf = snb_alloc();
    if (!sbuf) {
      break;
    }

    if (header.caplen <= SNBUF_DATA) {
      // pcap packet will fit in the mbuf, go ahead and copy.
      rte_memcpy(rte_pktmbuf_append(&sbuf->mbuf, header.caplen), packet,
                 header.caplen);
    } else {
/* FIXME: no support for chained mbuf for now */
#if 0
      /* Try read jumbo frame into multi mbufs. */
      if (unlikely(pcap_rx_jumbo(sbuf->mbuf.pool,
              &sbuf->mbuf,
              packet,
              header.caplen) == -1)) {
        //drop all the mbufs.
        snb_free(sbuf);
        break;
      }
#else
      snb_free(sbuf);
      break;
#endif
    }

    pkts[recv_cnt] = sbuf;
    recv_cnt++;
  }

  return recv_cnt;
}
