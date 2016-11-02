#include "pcap.h"

PCAPPort::~PCAPPort() {
  if (pcap_handle_.is_initialized()) {
    DeInit();
  }
}

// don't use
struct snobj* PCAPPort::Init(struct snobj* conf) {
  return nullptr;
}

pb_error_t PCAPPort::Init(const bess::protobuf::PCAPPortArg& arg) {
  if (pcap_handle_.is_initialized()) {
    return pb_error(EINVAL, "Device already initialized.");
  }

  const std::string dev = arg.dev();
  pcap_handle_ = PcapHandle(dev);

  if (pcap_handle_.is_initialized()) {
    return pb_errno(0);
  } else {
    return pb_error(EINVAL, "Error initializing device.");
  }
}

void PCAPPort::DeInit() {
  if (pcap_handle_.is_initialized()) {
    pcap_handle_.~PcapHandle();
  }
}

int PCAPPort::RecvPackets(queue_t qid, snb_array_t pkts, int cnt) {
  if (!pcap_handle_.is_initialized()) {
    return 0;
  }

  int recv_cnt = 0;
  struct snbuf* sbuf;

  assert(qid == 0);

  while (recv_cnt < cnt) {
    int caplen = 0;
    const u_char* packet = pcap_handle_.RecvPacket(&caplen);
    if (!packet) {
      break;
    }

    sbuf = snb_alloc();
    if (!sbuf) {
      break;
    }

    if (caplen <= SNBUF_DATA) {
      // pcap packet will fit in the mbuf, go ahead and copy.
      rte_memcpy(rte_pktmbuf_append(&sbuf->mbuf, caplen), packet, caplen);
    } else {
/* FIXME: no support for chained mbuf for now */
#if 0
      /* Try read jumbo frame into multi mbufs. */
      if (unlikely(pcap_rx_jumbo(sbuf->mbuf.pool,
              &sbuf->mbuf,
              packet,
              caplen) == -1)) {
        //drop all the mbufs.
        snb_free(sbuf);
        break;
      }
#else
      RTE_LOG(ERR, PMD, "Dropping PCAP packet: Size (%d) > max size (%d).\n",
              sbuf->mbuf.pkt_len, SNBUF_DATA);
      snb_free(sbuf);
      break;
#endif
    }

    pkts[recv_cnt] = sbuf;
    recv_cnt++;
  }

  return recv_cnt;
}

int PCAPPort::SendPackets(queue_t qid, snb_array_t pkts, int cnt) {
  if (!pcap_handle_.is_initialized()) {
    return 0;  // TODO: Would like to raise an error here...
  }

  int ret;
  int send_cnt = 0;

  assert(qid == 0);

  while (send_cnt < cnt) {
    struct snbuf* sbuf = pkts[send_cnt];

    if (likely(sbuf->mbuf.nb_segs == 1)) {
      ret = pcap_handle_.SendPacket((const u_char*)snb_head_data(sbuf),
                                    sbuf->mbuf.pkt_len);
    } else if (sbuf->mbuf.pkt_len <= PCAP_SNAPLEN) {
      unsigned char tx_pcap_data[PCAP_SNAPLEN];
      GatherData(tx_pcap_data, &sbuf->mbuf);
      ret = pcap_handle_.SendPacket(tx_pcap_data, sbuf->mbuf.pkt_len);
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

void PCAPPort::GatherData(unsigned char* data, struct rte_mbuf* mbuf) {
  uint16_t data_len = 0;

  while (mbuf) {
    rte_memcpy(data + data_len, rte_pktmbuf_mtod(mbuf, void*), mbuf->data_len);

    data_len += mbuf->data_len;
    mbuf = mbuf->next;
  }
}

ADD_DRIVER(PCAPPort, "pcap_port", "libpcap live packet capture from Linux port")
