#include "pcap.h"

#include <algorithm>
#include <string>

#include "../utils/pcap.h"

CommandResponse PCAPPort::Init(const bess::pb::PCAPPortArg& arg) {
  if (pcap_handle_.is_initialized()) {
    return CommandFailure(EINVAL, "Device already initialized.");
  }

  const std::string dev = arg.dev();
  pcap_handle_ = PcapHandle(dev);

  if (!pcap_handle_.is_initialized()) {
    return CommandFailure(EINVAL, "Error initializing device.");
  }

  if (pcap_handle_.SetBlocking(false)) {
    return CommandFailure(EINVAL, "Error initializing device.");
  }

  return CommandSuccess();
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

    int copy_len = std::min(caplen, static_cast<int>(sbuf->tailroom()));
    bess::utils::CopyInlined(sbuf->append(copy_len), packet, copy_len, true);

    packet += copy_len;
    caplen -= copy_len;
    bess::Packet* m = sbuf;

    int nb_segs = 1;
    while (caplen > 0) {
      m->set_next(bess::Packet::Alloc());
      m = m->next();
      nb_segs++;

      // no headroom needed in chained mbufs
      m->prepend(m->headroom());
      m->set_data_len(0);
      m->set_buffer(0);

      copy_len = std::min(caplen, static_cast<int>(m->tailroom()));
      bess::utils::Copy(m->append(copy_len), packet, copy_len, true);

      packet += copy_len;
      caplen -= copy_len;
    }
    sbuf->set_nb_segs(nb_segs);
    pkts[recv_cnt] = sbuf;
    recv_cnt++;
  }

  return recv_cnt;
}

int PCAPPort::SendPackets(queue_t, bess::Packet** pkts, int cnt) {
  if (!pcap_handle_.is_initialized()) {
    CHECK(0);  // raise an error
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
    bess::utils::CopyInlined(data, pkt->head_data(), pkt->head_len());

    data += pkt->head_len();
    pkt = reinterpret_cast<bess::Packet*>(pkt->next());
  }
}

ADD_DRIVER(PCAPPort, "pcap_port", "libpcap live packet capture from Linux port")
