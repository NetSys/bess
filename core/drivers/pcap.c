#include <pcap/pcap.h>
#include <rte_common.h>

#include "../port.h"

#define PCAP_IFNAME 16

struct pcap_priv {
    pcap_t *pcap_handle;
    char dev[PCAP_IFNAME];
    char errbuf[PCAP_ERRBUF_SIZE];
};

static int pcap_init_driver(struct driver *driver)
{
	return 0;
}


static struct snobj *pcap_init_port(struct port *p, struct snobj *conf)
{
    struct pcap_priv *priv = get_port_priv(p);

    if(snobj_eval_str(conf, "dev")) {
        strncpy(priv->dev, snobj_eval_str(conf, "dev"), PCAP_IFNAME);
    }
    else 
        return snobj_err(EINVAL, "PCAP need to set dev option");

    //non-blocking pcap
    priv->pcap_handle = pcap_open_live(priv->dev, BUFSIZ, 1, 0, priv->errbuf);
    if(priv->pcap_handle == NULL) {
        return snobj_err(ENODEV, "PCAP Open dev error: %s", priv->errbuf);
    }

    printf("PCAP: open dev %s\n", priv->dev);
    priv->locked = 0;

    return NULL;
}

static void pcap_deinit_port(struct port *p)
{
    struct pcap_priv *priv = get_port_priv(p);
    if(priv->pcap_handle) {
        pcap_close(priv->pcap_handle);
    }
    memset(priv->errbuf, 0, PCAP_ERRBUF_SIZE);
}


static int pcap_recv_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
    const u_char *packet;
    struct pcap_pkthdr header;
    struct pcap_priv *priv = get_port_priv(p);
    
    int recv_cnt = 0;
    struct snbuf *sbuf; 
  

    while(recv_cnt < cnt) {
        sbuf = snb_alloc(SNBUF_PFRAME);
        if(!sbuf) {
            //no sbuf, break;
            break;
        }

        packet = pcap_next(priv->pcap_handle, &header);

        if(!packet) {
            //no packets, break;
            snb_free(sbuf);
            break;
        }

        snb_append(sbuf, header.caplen);
        rte_memcpy(snb_head_data(sbuf), packet, header.caplen);

        pkts[recv_cnt] = sbuf;
        recv_cnt ++;
    }


    return recv_cnt;
}

static int pcap_send_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
    struct pcap_priv *priv = get_port_priv(p);
    
    int ret;
    int send_cnt = 0;


    while(send_cnt < cnt) {
        //TODO: for seg mbuf, we need do some merge here. 
        struct snbuf *sbuf = pkts[send_cnt];
        ret = pcap_sendpacket(priv->pcap_handle, 
                (const u_char*)snb_head_data(sbuf), snb_head_len(sbuf));
        if(ret) {
            //send failed
            break;
        }

        send_cnt++;
    }

    snb_free_bulk(pkts, send_cnt);
    return send_cnt;
}

static const struct driver pcap = {
	.name 		= "PCAP",
    .def_port_name = "pcap", 
	.priv_size	= sizeof(struct pcap_priv),
	.init_driver	= pcap_init_driver,
	.init_port 	= pcap_init_port,
	.deinit_port	= pcap_deinit_port,
	.recv_pkts 	= pcap_recv_pkts,
	.send_pkts 	= pcap_send_pkts,
};

ADD_DRIVER(pcap)
