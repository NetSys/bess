#include "pmd.h"

#include "../utils/format.h"

/*!
 * The following are deprecated. Ignore us.
 */
#define SN_TSO_SG 0
#define SN_HW_RXCSUM 0
#define SN_HW_TXCSUM 0

#if 0
static const struct rte_eth_conf default_eth_conf = {
    .link_speeds = ETH_LINK_SPEED_AUTONEG,
    .rxmode =
        {
            .mq_mode = ETH_MQ_RX_RSS,       /* doesn't matter for 1-queue */
            .max_rx_pkt_len = 0,            /* valid only if jumbo is on */
            .split_hdr_size = 0,            /* valid only if HS is on */
            .header_split = 0,              /* Header Split */
            .hw_ip_checksum = SN_HW_RXCSUM, /* IP checksum offload */
            .hw_vlan_filter = 0,            /* VLAN filtering */
            .hw_vlan_strip = 0,             /* VLAN strip */
            .hw_vlan_extend = 0,            /* Extended VLAN */
            .jumbo_frame = 0,               /* Jumbo Frame support */
            .hw_strip_crc = 1,              /* CRC stripped by hardware */
        },
    .txmode =
        {
            .mq_mode = ETH_MQ_TX_NONE,
        },
    .lpbk_mode = 0,
    .rx_adv_conf =
        {
            .rss_conf =
                {
                    .rss_key = nullptr,
                    .rss_key_len = 40,
                    /* TODO: query rte_eth_dev_info_get() to set this*/
                    .rss_hf =
                        ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP | ETH_RSS_SCTP,
                },
        },
    .tx_adv_conf = {},
    .dcb_capability_en = 0,
    .fdir_conf =
        {
            .mode = RTE_FDIR_MODE_NONE,
        },
    .intr_conf =
        {
            .lsc = 0,
        },
};
#endif

static const struct rte_eth_conf default_eth_conf() {
  struct rte_eth_conf ret = rte_eth_conf();

  ret.link_speeds = ETH_LINK_SPEED_AUTONEG;

  ret.rxmode = {
      .mq_mode = ETH_MQ_RX_RSS,       /* doesn't matter for 1-queue */
      .max_rx_pkt_len = 0,            /* valid only if jumbo is on */
      .split_hdr_size = 0,            /* valid only if HS is on */
      .header_split = 0,              /* Header Split */
      .hw_ip_checksum = SN_HW_RXCSUM, /* IP checksum offload */
      .hw_vlan_filter = 0,            /* VLAN filtering */
      .hw_vlan_strip = 0,             /* VLAN strip */
      .hw_vlan_extend = 0,            /* Extended VLAN */
      .jumbo_frame = 0,               /* Jumbo Frame support */
      .hw_strip_crc = 1,              /* CRC stripped by hardware */
      .enable_scatter = 0,            /* no scattered RX */
      .enable_lro = 0,                /* no large receive offload */
  };

  ret.rx_adv_conf.rss_conf = {
      .rss_key = nullptr,
      .rss_key_len = 40,
      /* TODO: query rte_eth_dev_info_get() to set this*/
      .rss_hf = ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP | ETH_RSS_SCTP,
  };

  return ret;
}

void PMDPort::InitDriver() {
  dpdk_port_t num_dpdk_ports = rte_eth_dev_count();

  LOG(INFO) << num_dpdk_ports << " DPDK PMD ports have been recognized:";

  for (dpdk_port_t i = 0; i < num_dpdk_ports; i++) {
    struct rte_eth_dev_info dev_info;
    std::string pci_info;

    rte_eth_dev_info_get(i, &dev_info);

    if (dev_info.pci_dev) {
      pci_info = bess::utils::Format(
          "%04hx:%02hhx:%02hhx.%02hhx %04hx:%04hx  ",
          dev_info.pci_dev->addr.domain, dev_info.pci_dev->addr.bus,
          dev_info.pci_dev->addr.devid, dev_info.pci_dev->addr.function,
          dev_info.pci_dev->id.vendor_id, dev_info.pci_dev->id.device_id);
    }

    LOG(INFO) << "DPDK port_id " << i << " (" << dev_info.driver_name
              << ")   RXQ " << dev_info.max_rx_queues << " TXQ "
              << dev_info.max_tx_queues << "  " << pci_info;
  }
}

static struct snobj *find_dpdk_port(struct snobj *conf,
                                    dpdk_port_t *ret_port_id,
                                    bool *ret_hot_plugged) {
  struct snobj *t;

  dpdk_port_t port_id = DPDK_PORT_UNKNOWN;

  if ((t = snobj_eval(conf, "port_id")) != nullptr) {
    if (snobj_type(t) != TYPE_INT) {
      return snobj_err(EINVAL, "Port ID must be an integer");
    }

    port_id = snobj_int_get(t);

    if (port_id >= RTE_MAX_ETHPORTS) {
      return snobj_err(EINVAL, "Invalid port id %d", port_id);
    }

    if (!rte_eth_devices[port_id].attached) {
      return snobj_err(ENODEV, "Port id %d is not available", port_id);
    }
  }

  if ((t = snobj_eval(conf, "pci")) != nullptr) {
    const char *bdf;
    struct rte_pci_addr addr;

    if (port_id != DPDK_PORT_UNKNOWN) {
      return snobj_err(EINVAL,
                       "You cannot specify both "
                       "'port_id' and 'pci' fields");
    }

    bdf = snobj_str_get(t);

    if (!bdf ||
        (eal_parse_pci_DomBDF(bdf, &addr) != 0 &&
         eal_parse_pci_BDF(bdf, &addr)) != 0) {
      return snobj_err(EINVAL,
                       "PCI address must be like "
                       "dddd:bb:dd.ff or bb:dd.ff");
    }

    for (int i = 0; i < RTE_MAX_ETHPORTS; i++) {
      if (rte_eth_devices[i].attached && rte_eth_devices[i].pci_dev &&
          !rte_eal_compare_pci_addr(&addr, &rte_eth_devices[i].pci_dev->addr)) {
        continue;
      }

      port_id = i;
      break;
    }

    /* If not found, maybe the device has not been attached yet */
    if (port_id == DPDK_PORT_UNKNOWN) {
      char name[RTE_ETH_NAME_MAX_LEN];
      int ret;

      snprintf(name, RTE_ETH_NAME_MAX_LEN, "%04x:%02x:%02x.%02x", addr.domain,
               addr.bus, addr.devid, addr.function);

      ret = rte_eth_dev_attach(name, &port_id);

      if (ret < 0) {
        return snobj_err(ENODEV,
                         "Cannot attach PCI "
                         "device %s",
                         name);
      }

      *ret_hot_plugged = true;
    }
  }

  if (port_id == DPDK_PORT_UNKNOWN &&
      (t = snobj_eval(conf, "vdev")) != nullptr) {
    const char *name = snobj_str_get(t);
    int ret = rte_eth_dev_attach(name, &port_id);

    if (ret < 0) {
      return snobj_err(ENODEV, "Cannot attach vdev %s", name);
    }

    *ret_hot_plugged = true;
  }

  if (port_id == DPDK_PORT_UNKNOWN) {
    return snobj_err(EINVAL,
                     "'port_id', 'pci', or 'vdev' field "
                     "must be specified");
  }

  *ret_port_id = port_id;
  return nullptr;
}

static pb_error_t find_dpdk_port(dpdk_port_t port_id, const std::string &pci,
                                 const std::string &vdev,
                                 dpdk_port_t *ret_port_id,
                                 bool *ret_hot_plugged) {
  if (pci.length() == 0) {
    if (port_id >= RTE_MAX_ETHPORTS) {
      return pb_error(EINVAL, "Invalid port id %d", port_id);
    }
    if (!rte_eth_devices[port_id].attached) {
      return pb_error(ENODEV, "Port id %d is not available", port_id);
    }
  } else {
    struct rte_pci_addr addr;
    if (port_id != DPDK_PORT_UNKNOWN) {
      return pb_error(EINVAL,
                      "You cannot specify both "
                      "'port_id' and 'pci' fields");
    }
    if (eal_parse_pci_DomBDF(pci.c_str(), &addr) != 0 &&
        eal_parse_pci_BDF(pci.c_str(), &addr) != 0) {
      return pb_error(EINVAL,
                      "PCI address must be like "
                      "dddd:bb:dd.ff or bb:dd.ff");
    }
    for (int i = 0; i < RTE_MAX_ETHPORTS; i++) {
      if (!rte_eth_devices[i].attached || !rte_eth_devices[i].pci_dev ||
          rte_eal_compare_pci_addr(&addr, &rte_eth_devices[i].pci_dev->addr)) {
        continue;
      }

      port_id = i;
      break;
    }

    /* If not found, maybe the device has not been attached yet */
    if (port_id == DPDK_PORT_UNKNOWN) {
      char name[RTE_ETH_NAME_MAX_LEN];
      int ret;

      snprintf(name, RTE_ETH_NAME_MAX_LEN, "%04x:%02x:%02x.%02x", addr.domain,
               addr.bus, addr.devid, addr.function);

      ret = rte_eth_dev_attach(name, &port_id);

      if (ret < 0) {
        return pb_error(ENODEV,
                        "Cannot attach PCI "
                        "device %s",
                        name);
      }

      *ret_hot_plugged = true;
    }
  }

  if (port_id == DPDK_PORT_UNKNOWN && vdev.length() != 0) {
    const char *name = vdev.c_str();
    int ret = rte_eth_dev_attach(name, &port_id);

    if (ret < 0) {
      return pb_error(ENODEV, "Cannot attach vdev %s", name);
    }

    *ret_hot_plugged = true;
  }

  if (port_id == DPDK_PORT_UNKNOWN) {
    return pb_error(EINVAL,
                    "'port_id', 'pci', or 'vdev' field "
                    "must be specified");
  }

  *ret_port_id = port_id;
  return pb_errno(0);
}

struct snobj *PMDPort::Init(struct snobj *conf) {
  dpdk_port_t port_id = -1;

  struct rte_eth_dev_info dev_info;
  struct rte_eth_conf eth_conf;
  struct rte_eth_rxconf eth_rxconf;
  struct rte_eth_txconf eth_txconf;

  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  struct snobj *err;

  int ret;

  int i;

  err = find_dpdk_port(conf, &port_id, &hot_plugged_);
  if (err) {
    return err;
  }

  eth_conf = default_eth_conf();
  if (snobj_eval_int(conf, "loopback")) {
    eth_conf.lpbk_mode = 1;
  }

  /* Use defaut rx/tx configuration as provided by PMD drivers,
   * with minor tweaks */
  rte_eth_dev_info_get(port_id, &dev_info);

  eth_rxconf = dev_info.default_rxconf;

  /* #36: em driver does not allow rx_drop_en enabled */
  if (strcmp(dev_info.driver_name, "rte_em_pmd") != 0) {
    eth_rxconf.rx_drop_en = 1;
  }

  eth_txconf = dev_info.default_txconf;
  eth_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL |
                         ETH_TXQ_FLAGS_NOMULTSEGS * (1 - SN_TSO_SG) |
                         ETH_TXQ_FLAGS_NOXSUMS * (1 - SN_HW_TXCSUM);

  ret = rte_eth_dev_configure(port_id, num_rxq, num_txq, &eth_conf);
  if (ret != 0) {
    return snobj_err(-ret, "rte_eth_dev_configure() failed");
  }

  rte_eth_promiscuous_enable(port_id);

  for (i = 0; i < num_rxq; i++) {
    int sid = rte_eth_dev_socket_id(port_id);

    /* if socket_id is invalid, set to 0 */
    if (sid < 0 || sid > RTE_MAX_NUMA_NODES) {
      sid = 0;
    }

    ret = rte_eth_rx_queue_setup(port_id, i, queue_size[PACKET_DIR_INC], sid,
                                 &eth_rxconf, get_pframe_pool_socket(sid));
    if (ret != 0) {
      return snobj_err(-ret, "rte_eth_rx_queue_setup() failed");
    }
  }

  for (i = 0; i < num_txq; i++) {
    int sid = 0; /* XXX */

    ret = rte_eth_tx_queue_setup(port_id, i, queue_size[PACKET_DIR_OUT], sid,
                                 &eth_txconf);
    if (ret != 0) {
      return snobj_err(-ret, "rte_eth_tx_queue_setup() failed");
    }
  }

  ret = rte_eth_dev_start(port_id);
  if (ret != 0) {
    return snobj_err(-ret, "rte_eth_dev_start() failed");
  }

  dpdk_port_id_ = port_id;

  return nullptr;
}

pb_error_t PMDPort::InitPb(const bess::pb::PMDPortArg &arg) {
  dpdk_port_t ret_port_id = -1;

  struct rte_eth_dev_info dev_info;
  struct rte_eth_conf eth_conf;
  struct rte_eth_rxconf eth_rxconf;
  struct rte_eth_txconf eth_txconf;

  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  int ret;

  int i;

  pb_error_t err = find_dpdk_port(arg.port_id(), arg.pci(), arg.vdev(),
                                  &ret_port_id, &hot_plugged_);
  if (err.err() != 0) {
    return err;
  }

  eth_conf = default_eth_conf();
  if (arg.loopback()) {
    eth_conf.lpbk_mode = 1;
  }

  /* Use defaut rx/tx configuration as provided by PMD drivers,
   * with minor tweaks */
  rte_eth_dev_info_get(ret_port_id, &dev_info);

  eth_rxconf = dev_info.default_rxconf;

  /* #36: em driver does not allow rx_drop_en enabled */
  if (strcmp(dev_info.driver_name, "rte_em_pmd") != 0) {
    eth_rxconf.rx_drop_en = 1;
  }

  eth_txconf = dev_info.default_txconf;
  eth_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL |
                         ETH_TXQ_FLAGS_NOMULTSEGS * (1 - SN_TSO_SG) |
                         ETH_TXQ_FLAGS_NOXSUMS * (1 - SN_HW_TXCSUM);

  ret = rte_eth_dev_configure(ret_port_id, num_rxq, num_txq, &eth_conf);
  if (ret != 0) {
    return pb_error(-ret, "rte_eth_dev_configure() failed");
  }
  rte_eth_promiscuous_enable(ret_port_id);

  for (i = 0; i < num_rxq; i++) {
    int sid = rte_eth_dev_socket_id(ret_port_id);

    /* if socket_id is invalid, set to 0 */
    if (sid < 0 || sid > RTE_MAX_NUMA_NODES) {
      sid = 0;
    }

    ret = rte_eth_rx_queue_setup(ret_port_id, i, queue_size[PACKET_DIR_INC],
                                 sid, &eth_rxconf, get_pframe_pool_socket(sid));
    if (ret != 0) {
      return pb_error(-ret, "rte_eth_rx_queue_setup() failed");
    }
  }

  for (i = 0; i < num_txq; i++) {
    int sid = 0; /* XXX */

    ret = rte_eth_tx_queue_setup(ret_port_id, i, queue_size[PACKET_DIR_OUT],
                                 sid, &eth_txconf);
    if (ret != 0) {
      return pb_error(-ret, "rte_eth_tx_queue_setup() failed");
    }
  }

  ret = rte_eth_dev_start(ret_port_id);
  if (ret != 0) {
    return pb_error(-ret, "rte_eth_dev_start() failed");
  }

  dpdk_port_id_ = ret_port_id;

  return pb_errno(0);
}

void PMDPort::DeInit() {
  rte_eth_dev_stop(dpdk_port_id_);

  if (hot_plugged_) {
    char name[RTE_ETH_NAME_MAX_LEN];
    int ret;

    rte_eth_dev_close(dpdk_port_id_);
    ret = rte_eth_dev_detach(dpdk_port_id_, name);
    if (ret < 0) {
      LOG(WARNING) << "rte_eth_dev_detach(" << dpdk_port_id_
                   << ") failed: " << rte_strerror(-ret);
    }
  }
}

void PMDPort::CollectStats(bool reset) {
  struct rte_eth_stats stats;
  int ret;

  packet_dir_t dir;
  queue_t qid;

  if (reset) {
    rte_eth_stats_reset(dpdk_port_id_);
    return;
  }

  ret = rte_eth_stats_get(dpdk_port_id_, &stats);
  if (ret < 0) {
    LOG(ERROR) << "rte_eth_stats_get(" << dpdk_port_id_
               << ") failed: " << rte_strerror(-ret);
    return;
  }

  VLOG(1) << bess::utils::Format(
      "PMD port %d: ipackets %lu opackets %lu ibytes %lu obytes %lu "
      "imissed %lu ierrors %lu oerrors %lu rx_nombuf %lu",
      dpdk_port_id_, stats.ipackets, stats.opackets, stats.ibytes,
      stats.obytes, stats.imissed, stats.ierrors,
      stats.oerrors, stats.rx_nombuf);

  port_stats[PACKET_DIR_INC].dropped = stats.imissed;

  dir = PACKET_DIR_INC;
  for (qid = 0; qid < num_queues[dir]; qid++) {
    queue_stats[dir][qid].packets = stats.q_ipackets[qid];
    queue_stats[dir][qid].bytes = stats.q_ibytes[qid];
    queue_stats[dir][qid].dropped = stats.q_errors[qid];
  }

  dir = PACKET_DIR_OUT;
  for (qid = 0; qid < num_queues[dir]; qid++) {
    queue_stats[dir][qid].packets = stats.q_opackets[qid];
    queue_stats[dir][qid].bytes = stats.q_obytes[qid];
  }
}

int PMDPort::RecvPackets(queue_t qid, snb_array_t pkts, int cnt) {
  return rte_eth_rx_burst(dpdk_port_id_, qid, (struct rte_mbuf **)pkts, cnt);
}

int PMDPort::SendPackets(queue_t qid, snb_array_t pkts, int cnt) {
  int sent =
      rte_eth_tx_burst(dpdk_port_id_, qid, (struct rte_mbuf **)pkts, cnt);

  port_stats[PACKET_DIR_OUT].dropped += (cnt - sent);

  return sent;
}

ADD_DRIVER(PMDPort, "pmd_port", "DPDK poll mode driver")
