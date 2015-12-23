#include <pcap/pcap.h>

#include "../port.h"

#define PCAP_IFNAME 16

#define PCAP_SNAPLEN ETHER_MAX_JUMBO_FRAME_LEN
#define PCAP_SNAPSHOT_LEN 65535

/* Experimental. Needs more tests */

struct pcap_priv {
	pcap_t *pcap_handle;
	char dev[PCAP_IFNAME];
};

static int pcap_init_driver(struct driver *driver)
{
	return 0;
}

static struct snobj *pcap_init_port(struct port *p, struct snobj *conf)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	struct pcap_priv *priv = get_port_priv(p);

	if(snobj_eval_str(conf, "dev")) {
		strncpy(priv->dev, snobj_eval_str(conf, "dev"), PCAP_IFNAME);
	}
	else 
		return snobj_err(EINVAL, "PCAP need to set dev option");

	//non-blocking pcap
	priv->pcap_handle = pcap_open_live(priv->dev, PCAP_SNAPLEN, 1, -1, errbuf);
	if(priv->pcap_handle == NULL) {
		return snobj_err(ENODEV, "PCAP Open dev error: %s", errbuf);
	}

	int ret = pcap_setnonblock(priv->pcap_handle, 1, errbuf);
	if(ret != 0) {
		return snobj_err(ENODEV, "PCAP set to nonblock error: %s", errbuf);
	}

	printf("PCAP: open dev %s\n", priv->dev);

	return NULL;
}

static void pcap_deinit_port(struct port *p)
{
	struct pcap_priv *priv = get_port_priv(p);
	if(priv->pcap_handle) {
		pcap_close(priv->pcap_handle);
		priv->pcap_handle = NULL;
	}
}

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
		len = RTE_MIN(rte_pktmbuf_tailroom(m), data_len);
		rte_memcpy(rte_pktmbuf_append(m, len), data, len);

		mbuf->nb_segs++;
		data_len -= len;
		data += len;
	}

	return mbuf->nb_segs;
}


static int 
pcap_recv_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
	const u_char *packet;
	struct pcap_pkthdr header;
	struct pcap_priv *priv = get_port_priv(p);

	int recv_cnt = 0;
	struct snbuf *sbuf; 

	while(recv_cnt < cnt) {
		packet = pcap_next(priv->pcap_handle, &header);
		if(!packet) {
			//no packets, break;
			break;
		}

		sbuf = snb_alloc();
		if(!sbuf) {
			//no sbuf, break;
			break;
		}

		if (header.caplen <= SNBUF_DATA) {
			/* pcap packet will fit in the mbuf, go ahead and copy */
			rte_memcpy(rte_pktmbuf_append(&sbuf->mbuf, header.caplen), packet,
					header.caplen);
		} else {
			/* FIXME: no support for chained mbuf for now */
			snb_free(sbuf);
			break;
			
			/* Try read jumbo frame into multi mbufs. */
			if (unlikely(pcap_rx_jumbo(sbuf->mbuf.pool,
							&sbuf->mbuf,
							packet,
							header.caplen) == -1)) {
				//drop all the mbufs.
				snb_free(sbuf);
				break;
			}
		}

		pkts[recv_cnt] = sbuf;
		recv_cnt ++;
	}


	return recv_cnt;
}

static unsigned char tx_pcap_data[PCAP_SNAPLEN];

/* Copy data from mbuf chain to a buffer suitable for writing to a PCAP file. */
static void pcap_gather_data(unsigned char *data, struct rte_mbuf *mbuf)
{
	uint16_t data_len = 0;

	while (mbuf) {
		rte_memcpy(data + data_len, rte_pktmbuf_mtod(mbuf, void *),
				mbuf->data_len);

		data_len += mbuf->data_len;
		mbuf = mbuf->next;
	}
}

static int pcap_send_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
	struct pcap_priv *priv = get_port_priv(p);

	int ret;
	int send_cnt = 0;


	while(send_cnt < cnt) {
		struct snbuf *sbuf = pkts[send_cnt];

		if (likely(sbuf->mbuf.nb_segs == 1)) {
			ret = pcap_sendpacket(priv->pcap_handle,
					(const u_char*)snb_head_data(sbuf),
					sbuf->mbuf.pkt_len);
		} else {
			if (sbuf->mbuf.pkt_len <= ETHER_MAX_JUMBO_FRAME_LEN) {
				pcap_gather_data(tx_pcap_data, &sbuf->mbuf);
				ret = pcap_sendpacket(priv->pcap_handle,
						tx_pcap_data,
						sbuf->mbuf.pkt_len);
			} else {
				RTE_LOG(ERR, PMD,
						"Dropping PCAP packet. "
						"Size (%d) > max jumbo size (%d).\n",
						sbuf->mbuf.pkt_len,
						ETHER_MAX_JUMBO_FRAME_LEN);

				break;
			}
		}

		if (unlikely(ret != 0))
			break;

		send_cnt++;
	}

	snb_free_bulk(pkts, send_cnt);
	return send_cnt;
}

static const struct driver pcap = {
	.name 		= "PCAP",
	.def_port_name	= "pcap", 
	.priv_size	= sizeof(struct pcap_priv),
	.init_driver	= pcap_init_driver,
	.init_port 	= pcap_init_port,
	.deinit_port	= pcap_deinit_port,
	.recv_pkts 	= pcap_recv_pkts,
	.send_pkts 	= pcap_send_pkts,
};

ADD_DRIVER(pcap)
