#include <errno.h>
#include <pcap/pcap.h>

#include <glog/logging.h>

#include "../message.h"
#include "../port.h"
#include "../utils/pcap.h"

static unsigned char tx_pcap_data[PCAP_SNAPLEN];

/* Experimental. Needs more tests */
class PCAPPort : public Port {
 public:
  PCAPPort() : Port(), pcap_handle_() {}

  virtual pb_error_t Init(const bess::PCAPPortArg &arg);
  virtual void DeInit();

  virtual int RecvPackets(queue_t qid, snb_array_t pkts, int cnt);
  virtual int SendPackets(queue_t qid, snb_array_t pkts, int cnt);

  static void GatherData(unsigned char *data, struct rte_mbuf *mbuf);

 private:
  pcap_t *pcap_handle_;
};

pb_error_t PCAPPort::Init(const bess::PCAPPortArg &arg) {
  char errbuf[PCAP_ERRBUF_SIZE];

  const std::string dev = arg.dev();
  if (dev.length() == 0) {
    return pb_error(EINVAL, "PCAP need to set dev option");
  }
  // Non-blocking pcap.
  pcap_handle_ = pcap_open_live(dev.c_str(), PCAP_SNAPLEN, 1, -1, errbuf);
  if (pcap_handle_ == NULL) {
    return pb_error(ENODEV, "PCAP Open dev error: %s", errbuf);
  }

  int ret = pcap_setnonblock(pcap_handle_, 1, errbuf);
  if (ret != 0) {
    return pb_error(ENODEV, "PCAP set to nonblock error: %s", errbuf);
  }

  LOG(INFO) << "PCAP: open dev " << dev;

  return pb_errno(0);
}

void PCAPPort::DeInit() {
  if (pcap_handle_) {
    pcap_close(pcap_handle_);
    pcap_handle_ = NULL;
  }
}

#if 0
static int pcap_rx_jumbo(struct rte_mempool *mb_pool,
		struct rte_mbuf *mbuf,
		const u_char *data,
		uint16_t data_len)
{
	struct rte_mbuf *m = mbuf;

	/* Copy the first segment. */
	uint16_t len = rte_pktmbuf_tailroom(mbuf);

	rte_memcpy(rte_pktmbuf_append(mbuf, len), data, len);
	data_len -= len;
	data += len;

	while (data_len > 0) {
		/* Allocate next mbuf and point to that. */
		m->next = rte_pktmbuf_alloc(mb_pool);

		if (unlikely(!m->next))
			return -1;

		m = m->next;

		/* Headroom is needed for VPort TX */

		m->pkt_len = 0;
		m->data_len = 0;

		/* Copy next segment. */
		len = MIN(rte_pktmbuf_tailroom(m), data_len);
		rte_memcpy(rte_pktmbuf_append(m, len), data, len);

		mbuf->nb_segs++;
		data_len -= len;
		data += len;
	}

	return mbuf->nb_segs;
}
#endif

int PCAPPort::RecvPackets(queue_t qid, snb_array_t pkts, int cnt) {
  const u_char *packet;
  struct pcap_pkthdr header;

  int recv_cnt = 0;
  struct snbuf *sbuf;

  while (recv_cnt < cnt) {
    packet = pcap_next(pcap_handle_, &header);
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
        PCAPPort::GatherData(tx_pcap_data, &sbuf->mbuf);
        ret = pcap_sendpacket(pcap_handle_, tx_pcap_data, sbuf->mbuf.pkt_len);
      } else {
        RTE_LOG(ERR, PMD,
                "Dropping PCAP packet. "
                "Size (%d) > max jumbo size (%d).\n",
                sbuf->mbuf.pkt_len, PCAP_SNAPLEN);
        break;
      }
    }

    if (unlikely(ret != 0)) {
      break;
    }

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
