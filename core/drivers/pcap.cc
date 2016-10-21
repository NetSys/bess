#include <errno.h>
#include <pcap/pcap.h>

#include <string>

#include <glog/logging.h>

#include "../log.h"
#include "../port.h"
#include "../utils/pcap.h"

// TODO(barath): Add a class comment.
class PCAPPort : public Port {
 public:
  // TODO(barath): Add a comment for this constant.
  static const int kPCAPIfname = 16;

  PCAPPort() : Port(), pcap_handle_(), dev_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual void DeInit();

  virtual int RecvPackets(queue_t qid, snb_array_t pkts, int cnt);
  virtual int SendPackets(queue_t qid, snb_array_t pkts, int cnt);

 private:
  void GatherData(unsigned char *data, struct rte_mbuf *mbuf);

  pcap_t *pcap_handle_;  // TODO(barath): Add comment.
  std::string dev_;  // TODO(barath): Add comment.
};

struct snobj *PCAPPort::Init(struct snobj *conf) {
  char errbuf[PCAP_ERRBUF_SIZE];
  if (snobj_eval_str(conf, "dev")) {
    dev_ = std::string(snobj_eval_str(conf, "dev"), kPCAPIfname);
  } else {
    return snobj_err(EINVAL, "PCAP need to set dev option");
  }

  // non-blocking pcap
  pcap_handle_ = pcap_open_live(dev_.c_str(), PCAP_SNAPLEN, 1, -1, errbuf);
  if (pcap_handle_ == NULL) {
    return snobj_err(ENODEV, "PCAP Open dev error: %s", errbuf);
  }

  int ret = pcap_setnonblock(pcap_handle_, 1, errbuf);
  if (ret != 0) {
    return snobj_err(ENODEV, "PCAP set to nonblock error: %s", errbuf);
  }

  LOG(INFO) << "PCAP: open dev " << dev_;

  return NULL;
}

void PCAPPort::DeInit() {
  if (pcap_handle_) {
    pcap_close(pcap_handle_);
    pcap_handle_ = NULL;
  }
}

int PCAPPort::RecvPackets(queue_t qid, snb_array_t pkts, int cnt) {
  const u_char *packet;
  struct pcap_pkthdr header;

  int recv_cnt = 0;
  struct snbuf *sbuf;

  while (recv_cnt < cnt) {
    packet = pcap_next(pcap_handle_, &header);
    if (!packet) break;

    sbuf = snb_alloc();
    if (!sbuf) break;

    if (header.caplen <= SNBUF_DATA) {
      /* pcap packet will fit in the mbuf, go ahead and copy */
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

int PCAPPort::SendPackets(queue_t qid, snb_array_t pkts, int cnt) {
  int ret;
  int send_cnt = 0;

  while (send_cnt < cnt) {
    struct snbuf *sbuf = pkts[send_cnt];

    if (likely(sbuf->mbuf.nb_segs == 1)) {
      ret = pcap_sendpacket(pcap_handle_, (const u_char *)snb_head_data(sbuf),
                            sbuf->mbuf.pkt_len);
    } else {
      if (sbuf->mbuf.pkt_len <= PCAP_SNAPLEN) {
        unsigned char tx_pcap_data[PCAP_SNAPLEN];
        GatherData(tx_pcap_data, &sbuf->mbuf);
        ret = pcap_sendpacket(pcap_handle_, tx_pcap_data, sbuf->mbuf.pkt_len);
      } else {
        RTE_LOG(ERR, PMD,
                "Dropping PCAP packet. "
                "Size (%d) > max jumbo size (%d).\n",
                sbuf->mbuf.pkt_len, PCAP_SNAPLEN);
        break;
      }
    }

    if (unlikely(ret != 0)) break;

    send_cnt++;
  }

  snb_free_bulk(pkts, send_cnt);
  return send_cnt;
}

// Copy data from mbuf chain to a buffer suitable for writing to a PCAP file.
void PCAPPort::GatherData(unsigned char *data, struct rte_mbuf *mbuf) {
  uint16_t data_len = 0;

  while (mbuf) {
    rte_memcpy(data + data_len, rte_pktmbuf_mtod(mbuf, void *), mbuf->data_len);

    data_len += mbuf->data_len;
    mbuf = mbuf->next;
  }
}

ADD_DRIVER(PCAPPort, "pcap_port", "libpcap live packet capture from Linux port")
