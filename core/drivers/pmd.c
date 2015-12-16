#include <rte_config.h>
#include <rte_ethdev.h>
#include <rte_errno.h>

#include "../port.h"

#define DPDK_PORT_UNKNOWN	RTE_MAX_ETHPORTS

typedef uint8_t dpdk_port_t;

struct pmd_priv {
	dpdk_port_t dpdk_port_id;
	int stats;
};

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
		/* TODO: query rte_eth_dev_info_get() to set this*/
		.rss_hf = ETH_RSS_IP |
			  ETH_RSS_UDP |
			  ETH_RSS_TCP |
			  ETH_RSS_SCTP,
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
	dpdk_port_t num_dpdk_ports = rte_eth_dev_count();

	printf("%d DPDK PMD ports have been recognized:\n", num_dpdk_ports);

	for (dpdk_port_t i = 0; i < num_dpdk_ports; i++) {
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

static struct snobj *find_dpdk_port(struct snobj *conf, 
		dpdk_port_t *ret_port_id)
{
	struct snobj *t;

	dpdk_port_t port_id = DPDK_PORT_UNKNOWN;

	if ((t = snobj_eval(conf, "port_id")) != NULL) {
		if (snobj_type(t) != TYPE_INT)
			return snobj_err(EINVAL, "Port ID must be an integer");

		port_id = snobj_int_get(t);

		if (port_id < 0 || port_id >= RTE_MAX_ETHPORTS)
			return snobj_err(EINVAL, "Invalid port id %d",
					port_id);
		
		if (!rte_eth_devices[port_id].attached)
			return snobj_err(ENODEV, "Port id %d is not available",
					port_id);
	}

	if ((t = snobj_eval(conf, "pci")) != NULL) {
		const char *bdf;
		struct rte_pci_addr addr;

		if (port_id != DPDK_PORT_UNKNOWN)
			return snobj_err(EINVAL, "You cannot specify both " \
					"'port_id' and 'pci' fields");

		bdf = snobj_str_get(t);

		if (!bdf) {
pci_format_err:
			return snobj_err(EINVAL, "PCI address must be like " \
					"dddd:bb:dd.ff or bb:dd.ff");
		}

		if (eal_parse_pci_DomBDF(bdf, &addr) != 0 && 
				eal_parse_pci_BDF(bdf, &addr) != 0)
			goto pci_format_err;

		for (int i = 0; i < RTE_MAX_ETHPORTS; i++) {
			if (!rte_eth_devices[i].attached)
				continue;

			if (!rte_eth_devices[i].pci_dev)
				continue;

			if (rte_eal_compare_pci_addr(&addr, 
					&rte_eth_devices[i].pci_dev->addr))
				continue;

			port_id = i;
			break;
		}

		/* If not found, maybe the device has not been attached yet */
		if (port_id == DPDK_PORT_UNKNOWN) {
			char devargs[1024];
			int ret;

			sprintf(devargs, "%04x:%02x:%02x.%02x",
					addr.domain,
					addr.bus,
					addr.devid,
					addr.function);

			ret = rte_eth_dev_attach(devargs, &port_id);

			if (ret < 0)
				return snobj_err(ENODEV, "Cannot attach PCI " \
						"device %s", devargs);
		}
	}

	if (port_id == DPDK_PORT_UNKNOWN)
		return snobj_err(EINVAL, "Either 'port_id' or 'pci' field " \
				"must be specified");

	*ret_port_id = port_id;
	return NULL;
}

static struct snobj *pmd_init_port(struct port *p, struct snobj *conf)
{
	struct pmd_priv *priv = get_port_priv(p);

	dpdk_port_t port_id = -1;

	struct rte_eth_dev_info dev_info = {};
	struct rte_eth_conf eth_conf;
	struct rte_eth_rxconf eth_rxconf;
	struct rte_eth_txconf eth_txconf;
#if 0
	struct rte_eth_fc_conf fc_conf;
#endif

	int num_txq = p->num_queues[PACKET_DIR_OUT];
	int num_rxq = p->num_queues[PACKET_DIR_INC];

	struct snobj *err;
	
	int ret;

	int i;

	err = find_dpdk_port(conf, &port_id);
	if (err)
		return err;

	eth_conf = default_eth_conf;
	if (snobj_eval_int(conf, "loopback"))
		eth_conf.lpbk_mode = 1;

	/* Use defaut rx/tx configuration as provided by PMD drivers,
	 * with minor tweaks */
	rte_eth_dev_info_get(port_id, &dev_info);

	eth_rxconf = dev_info.default_rxconf;
	eth_rxconf.rx_drop_en = 1;

	eth_txconf = dev_info.default_txconf;
	eth_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL |
			ETH_TXQ_FLAGS_NOMULTSEGS * (1 - SN_TSO_SG) | 
			ETH_TXQ_FLAGS_NOXSUMS * (1 - SN_HW_TXCSUM);

	ret = rte_eth_dev_configure(port_id,
				    num_rxq, num_txq, &eth_conf);
	if (ret != 0) 
		return snobj_err(-ret, "rte_eth_dev_configure() failed");

	rte_eth_promiscuous_enable(port_id);

	for (i = 0; i < num_rxq; i++) {
		int sid = rte_eth_dev_socket_id(port_id);

		/* if socket_id is invalid, set to 0 */
		if (sid < 0 || sid > RTE_MAX_NUMA_NODES)
			sid = 0;

		ret = rte_eth_rx_queue_setup(port_id, i, 
					     p->queue_size[PACKET_DIR_INC],
					     sid, &eth_rxconf,
					     get_pframe_pool_socket(sid));
		if (ret != 0) 
			return snobj_err(-ret, 
					"rte_eth_rx_queue_setup() failed");
	}

	for (i = 0; i < num_txq; i++) {
		int sid = 0;		/* XXX */

		ret = rte_eth_tx_queue_setup(port_id, i,
					     p->queue_size[PACKET_DIR_OUT],
					     sid, &eth_txconf);
		if (ret != 0) 
			return snobj_err(-ret,
					"rte_eth_tx_queue_setup() failed");
	}

#if 0
	ret = rte_eth_dev_flow_ctrl_get(port_id, &fc_conf);
	if (ret != 0) 
		return snobj_err(-ret, "rte_eth_dev_flow_ctrl_get() failed");

	printf("port %d high %u low %u ptime %hu send_xon %hu mode %u cfwd %hhu autoneg %hhu\n",
			port_id,
			fc_conf.high_water, fc_conf.low_water,
			fc_conf.pause_time, fc_conf.send_xon, fc_conf.mode,
			fc_conf.mac_ctrl_frame_fwd, fc_conf.autoneg);
#endif

	ret = rte_eth_dev_start(port_id);
	if (ret != 0) 
		return snobj_err(-ret, "rte_eth_dev_start() failed");

	priv->dpdk_port_id = port_id;

	return NULL;
}

static void pmd_deinit_port(struct port *p)
{
	struct pmd_priv *priv = get_port_priv(p);

	rte_eth_dev_stop(priv->dpdk_port_id);
}

static void pmd_collect_stats(struct port *p, int reset)
{
	struct pmd_priv *priv = get_port_priv(p);

	struct rte_eth_stats stats;
	int ret;

	packet_dir_t dir;
	queue_t qid;

	if (reset) {
		rte_eth_stats_reset(priv->dpdk_port_id);
		return;
	}

	ret = rte_eth_stats_get(priv->dpdk_port_id, &stats);
	if (ret < 0) {
		fprintf(stderr, "rte_eth_stats_get() failed: %s\n",
				rte_strerror(rte_errno));
		return;
	}

#if 0
	printf("PMD port %d: "
	       "ipackets %lu opackets %lu ibytes %lu obytes %lu "
	       "imissed %lu ibadcrc %lu ibadlen %lu ierrors %lu oerrors %lu "
	       "imcasts %lu rx_nombuf %lu fdirmatch %lu fdirmiss %lu "
	       "tx_pause_xon %lu rx_pause_xon %lu "
	       "tx_pause_xoff %lu rx_pause_xoff %lu\n",
			priv->dpdk_port_id,
			stats.ipackets, stats.opackets, 
			stats.ibytes, stats.obytes,
			stats.imissed, stats.ibadcrc, stats.ibadlen, 
			stats.ierrors, stats.oerrors, stats.imcasts,
			stats.rx_nombuf, stats.fdirmatch, stats.fdirmiss,
			stats.tx_pause_xon, stats.rx_pause_xon,
			stats.tx_pause_xoff, stats.rx_pause_xoff);
#endif

	p->port_stats[PACKET_DIR_INC].dropped = stats.imissed;

	dir = PACKET_DIR_INC;
	for (qid = 0; qid < p->num_queues[dir]; qid++) {
		p->queue_stats[dir][qid].packets = stats.q_ipackets[qid]; 
		p->queue_stats[dir][qid].bytes   = stats.q_ibytes[qid]; 
		p->queue_stats[dir][qid].dropped = stats.q_errors[qid]; 
	}

	dir = PACKET_DIR_OUT;
	for (qid = 0; qid < p->num_queues[dir]; qid++) {
		p->queue_stats[dir][qid].packets = stats.q_opackets[qid]; 
		p->queue_stats[dir][qid].bytes   = stats.q_obytes[qid]; 
	}
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

	int sent = rte_eth_tx_burst(priv->dpdk_port_id, qid, 
			(struct rte_mbuf **)pkts, cnt);

	p->port_stats[PACKET_DIR_OUT].dropped += (cnt - sent);

	return sent;
}

static const struct driver pmd = {
	.name 		= "PMD",
	.priv_size	= sizeof(struct pmd_priv),
	.def_size_inc_q = 256,
	.def_size_out_q = 256,
	.flags		= DRIVER_FLAG_SELF_INC_STATS |
			  DRIVER_FLAG_SELF_OUT_STATS,
	.init_driver	= pmd_init_driver,
	.init_port 	= pmd_init_port,
	.deinit_port	= pmd_deinit_port,
	.collect_stats	= pmd_collect_stats,
	.recv_pkts 	= pmd_recv_pkts,
	.send_pkts 	= pmd_send_pkts,
};

ADD_DRIVER(pmd)
