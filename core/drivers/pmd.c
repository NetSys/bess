#include <rte_config.h>
#include <rte_ethdev.h>

#include "../port.h"

struct pmd_priv {
	int dpdk_port_id;
	int stats;
};

/* TODO: runtime changeable */
#define NUM_RXD 128
#define NUM_TXD 512

#define SN_TSO_SG		0
#define SN_HW_RXCSUM		0
#define SN_HW_TXCSUM		0

static const struct rte_eth_conf default_eth_conf = {
	.link_speed = ETH_LINK_SPEED_AUTONEG,
	.link_duplex = ETH_LINK_AUTONEG_DUPLEX,	/* auto negotiation */
	.lpbk_mode = 0,
	.rxmode = {
		.mq_mode = ETH_MQ_RX_RSS,	/* doesn't matter for 1-queue */
		.max_rx_pkt_len = 0,		/* valid only if jumbo is on */
		.split_hdr_size = 0,		/* valid only if HS is on */
		.header_split = 0,      	/* Header Split */
		.hw_ip_checksum = SN_HW_RXCSUM, /* IP checksum offload */
		.hw_vlan_filter = 0,    	/* VLAN filtering */
		.hw_vlan_strip = 0,		/* VLAN strip */
		.hw_vlan_extend = 0,		/* Extended VLAN */
		.jumbo_frame = 0,       	/* Jumbo Frame support */
		.hw_strip_crc = 1,      	/* CRC stripped by hardware */
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
	.rx_adv_conf.rss_conf = {
		.rss_hf = ETH_RSS_IPV4 |
			  ETH_RSS_IPV6 |
			  ETH_RSS_IPV6_EX |
			  ETH_RSS_IPV6_TCP_EX |
			  ETH_RSS_IPV6_EX |
			  ETH_RSS_IPV6_UDP_EX,
		.rss_key = NULL,
	},
	.fdir_conf = {
		.mode = RTE_FDIR_MODE_NONE,
	},
	.intr_conf= {
		.lsc = 0,
	},
};

static int pmd_init_driver(struct driver *driver)
{
	int num_dpdk_ports = rte_eth_dev_count();
	int i;

	printf("%d DPDK PMD ports have been recognized:\n", num_dpdk_ports);

	for (i = 0; i < num_dpdk_ports; i++) {
		struct rte_eth_dev_info dev_info;

		memset(&dev_info, 0, sizeof(dev_info));
		rte_eth_dev_info_get(i, &dev_info);

		printf("DPDK port_id %d (%s)   RXQ %hu TXQ %hu  ", 
				i, 
				dev_info.driver_name,
				dev_info.max_rx_queues,
				dev_info.max_tx_queues);

		if (dev_info.pci_dev) {
			printf("%04hx:%02hhx:%02hhx.%02hhx %04hx:%04hx  ",
				dev_info.pci_dev->addr.domain,
				dev_info.pci_dev->addr.bus,
				dev_info.pci_dev->addr.devid,
				dev_info.pci_dev->addr.function,
				dev_info.pci_dev->id.vendor_id,
				dev_info.pci_dev->id.device_id);
		}

		printf("\n");
	}

	return 0;
}

static struct snobj *pmd_init_port(struct port *p, struct snobj *conf)
{
	struct pmd_priv *priv = get_port_priv(p);

	int port_id = 0;

	struct rte_eth_dev_info dev_info = {};
	struct rte_eth_conf eth_conf;
	struct rte_eth_rxconf eth_rxconf;
	struct rte_eth_txconf eth_txconf;

	/* XXX */
	int num_txq = 1;
	int num_rxq = 1;

	int ret;

	int i;

	if (snobj_eval_exists(conf, "port_id"))
		port_id = snobj_eval_int(conf, "port_id");
	/* TODO: accept PCI BDF as well */

	eth_conf = default_eth_conf;
	if (snobj_eval_int(conf, "loopback"))
		eth_conf.lpbk_mode = 1;

	if (port_id >= RTE_MAX_ETHPORTS || !rte_eth_devices[port_id].attached)
		return snobj_err(ENODEV, "DPDK port id %d is not available",
				port_id);

	/* Use defaut rx/tx configuration as provided by PMD drivers,
	 * with minor tweaks */
	rte_eth_dev_info_get(port_id, &dev_info);

	eth_rxconf = dev_info.default_rxconf;
	eth_rxconf.rx_drop_en = 1;

	eth_txconf = dev_info.default_txconf;
	eth_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL |
			ETH_TXQ_FLAGS_NOMULTSEGS * (1 - SN_TSO_SG) | 
			ETH_TXQ_FLAGS_NOXSUMS * (1 - SN_HW_RXCSUM);

	ret = rte_eth_dev_configure(port_id,
				    num_txq, num_rxq, &eth_conf);
	if (ret != 0) 
		return snobj_errno_details(-ret, conf);

	rte_eth_promiscuous_enable(port_id);

	for (i = 0; i < num_rxq; i++) {
		int sid = 0;		/* XXX */

		ret = rte_eth_rx_queue_setup(port_id, i,
					     NUM_RXD, sid, &eth_rxconf,
					     get_pframe_pool_socket(sid));
		if (ret != 0) 
			return snobj_errno_details(-ret, conf);
	}

	for (i = 0; i < num_txq; i++) {
		int sid = 0;		/* XXX */

		ret = rte_eth_tx_queue_setup(port_id, i,
					     NUM_TXD, sid, &eth_txconf);
		if (ret != 0) 
			return snobj_errno_details(-ret, conf);
	}

	ret = rte_eth_dev_start(port_id);
	if (ret != 0) 
		return snobj_errno_details(-ret, conf);

	priv->dpdk_port_id = port_id;

	return NULL;
}

static void pmd_deinit_port(struct port *p)
{
	struct pmd_priv *priv = get_port_priv(p);

	rte_eth_dev_stop(priv->dpdk_port_id);
}

static int pmd_recv_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
	struct pmd_priv *priv = get_port_priv(p);

	return rte_eth_rx_burst(priv->dpdk_port_id, qid, 
			(struct rte_mbuf **)pkts, cnt);
}

static int pmd_send_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
	struct pmd_priv *priv = get_port_priv(p);

	return rte_eth_tx_burst(priv->dpdk_port_id, qid, 
			(struct rte_mbuf **)pkts, cnt);
}

static const struct driver pmd = {
	.name 		= "PMD",
	.priv_size	= sizeof(struct pmd_priv),
	.init_driver	= pmd_init_driver,
	.init_port 	= pmd_init_port,
	.deinit_port	= pmd_deinit_port,
	.recv_pkts 	= pmd_recv_pkts,
	.send_pkts 	= pmd_send_pkts,
};

ADD_DRIVER(pmd)
