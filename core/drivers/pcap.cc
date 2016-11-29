#include "pcap.h"

#include "../utils/pcap.h"

pb_error_t PCAPPort::Init(const bess::pb::PCAPPortArg& arg) {
  if (pcap_handle_.is_initialized()) {
    return pb_error(EINVAL, "Device already initialized.");
  }

  const std::string dev = arg.dev();
  pcap_handle_ = PcapHandle(dev);

  if (!pcap_handle_.is_initialized()) {
    return pb_error(EINVAL, "Error initializing device.");
  }

  if (pcap_handle_.SetBlocking(false)) {
    return pb_error(EINVAL, "Error initializing device.");
  }

  return pb_errno(0);
}

void PCAPPort::DeInit() {
  pcap_handle_.Reset();
}

int PCAPPort::RecvPackets(queue_t qid, bess::Packet** pkts, int cnt) {
  if (!pcap_handle_.is_initialized()) {
    return 0;
  }

  int recv_cnt = 0;
  bess::Packet* sbuf;

  DCHECK_EQ(qid, 0);

  while (recv_cnt < cnt) {
    int caplen = 0;
    const u_char* packet = pcap_handle_.RecvPacket(&caplen);
    if (!packet) {
      break;
    }

    sbuf = bess::Packet::Alloc();
    if (!sbuf) {
      break;
    }

    if (caplen <= SNBUF_DATA) {
      // pcap packet will fit in the mbuf, go ahead and copy.
      rte_memcpy(sbuf->append(caplen), packet, caplen);
    } else {
/* FIXME: no support for chained mbuf for now */
#if 0
      /* Try read jumbo frame into multi mbufs. */
      if (unlikely(pcap_rx_jumbo(sbuf->mbuf.pool,
              &sbuf->mbuf,
              packet,
              caplen) == -1)) {
        //drop all the mbufs.
        bess::Packet::Free(sbuf);
        break;
      }
#else
      RTE_LOG(ERR, PMD, "Dropping PCAP packet: Size (%d) > max size (%d).\n",
              sbuf->total_len(), SNBUF_DATA);
      bess::Packet::Free(sbuf);
      break;
#endif
    }

    pkts[recv_cnt] = sbuf;
    recv_cnt++;
  }

  return recv_cnt;
}

int PCAPPort::SendPackets(queue_t, bess::Packet** pkts, int cnt) {
  if (!pcap_handle_.is_initialized()) {
    return 0;  // TODO: Would like to raise an error here...
  }

  int sent = 0;

  while (sent < cnt) {
    bess::Packet* sbuf = pkts[sent];

    if (likely(sbuf->nb_segs() == 1)) {
      pcap_handle_.SendPacket(sbuf->head_data<const u_char*>(),
                              sbuf->total_len());
    } else if (sbuf->total_len() <= PCAP_SNAPLEN) {
      unsigned char tx_pcap_data[PCAP_SNAPLEN];
      GatherData(tx_pcap_data, sbuf);
      pcap_handle_.SendPacket(tx_pcap_data, sbuf->total_len());
    }

    sent++;
  }

  bess::Packet::Free(pkts, sent);
  return sent;
}

void PCAPPort::GatherData(unsigned char* data, bess::Packet* pkt) {
  while (pkt) {
    rte_memcpy(data, pkt->head_data(), pkt->head_len());

    data += pkt->head_len();
    pkt = reinterpret_cast<bess::Packet*>(pkt->next());
  }
}

ADD_DRIVER(PCAPPort, "pcap_port", "libpcap live packet capture from Linux port")
