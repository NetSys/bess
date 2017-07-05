// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "sn_common.h"
#include "sn_kernel.h"
#include "../snbuf_layout.h"

#define NUM_STATS_PER_TX_QUEUE (sizeof(struct sn_queue_tx_stats) / sizeof(u64))
#define NUM_STATS_PER_RX_QUEUE (sizeof(struct sn_queue_rx_stats) / sizeof(u64))

static int sn_ethtool_get_sset_count(struct net_device *netdev, int sset)
{
	struct sn_device *dev = netdev_priv(netdev);

	switch(sset) {
	case ETH_SS_STATS:
		return NUM_STATS_PER_TX_QUEUE * dev->num_txq +
			NUM_STATS_PER_RX_QUEUE * dev->num_rxq;
	default:
		return -EOPNOTSUPP;
	};
}

static void sn_ethtool_get_strings(struct net_device *netdev,
				  u32 sset, u8 *p)
{
	struct sn_device *dev = netdev_priv(netdev);
	int i;

	BUILD_BUG_ON(NUM_STATS_PER_TX_QUEUE != 5);
	BUILD_BUG_ON(NUM_STATS_PER_RX_QUEUE != 6);

	if (sset != ETH_SS_STATS)
		return;

	/* Use similar naming to ixgbe,
	 * so that we can reuse the same monitoring script */

	for (i = 0; i < dev->num_txq; i++) {
		sprintf(p, "tx_queue_%u_packets", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_queue_%u_bytes", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_queue_%u_drops", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_queue_%u_throttled", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_queue_%u_descdropped", i);
		p += ETH_GSTRING_LEN;
	}

	for (i = 0; i < dev->num_rxq; i++) {
		sprintf(p, "rx_queue_%u_packets", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "rx_queue_%u_bytes", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "rx_queue_%u_drops", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "rx_queue_%u_polls", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "rx_queue_%u_interrupts", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "rx_queue_%u_llpolls", i);
		p += ETH_GSTRING_LEN;
	}
}

static void sn_ethtool_get_ethtool_stats(struct net_device *netdev,
					 struct ethtool_stats *stats,
					 u64 *data)
{
	struct sn_device *dev = netdev_priv(netdev);
	int i;

	BUILD_BUG_ON(NUM_STATS_PER_TX_QUEUE != 5);
	BUILD_BUG_ON(NUM_STATS_PER_RX_QUEUE != 6);

	for (i = 0; i < dev->num_txq; i++) {
		data[0] = dev->tx_queues[i]->tx.stats.packets;
		data[1] = dev->tx_queues[i]->tx.stats.bytes;
		data[2] = dev->tx_queues[i]->tx.stats.dropped;
		data[3] = dev->tx_queues[i]->tx.stats.throttled;
		data[4] = dev->tx_queues[i]->tx.stats.descriptor;
		data += NUM_STATS_PER_TX_QUEUE;
	}

	for (i = 0; i < dev->num_rxq; i++) {
		dev->rx_queues[i]->rx.stats.dropped =
				dev->rx_queues[i]->rx.rx_regs->dropped;

		data[0] = dev->rx_queues[i]->rx.stats.packets;
		data[1] = dev->rx_queues[i]->rx.stats.bytes;
		data[2] = dev->rx_queues[i]->rx.stats.dropped;
		data[3] = dev->rx_queues[i]->rx.stats.polls;
		data[4] = dev->rx_queues[i]->rx.stats.interrupts;
		data[5] = dev->rx_queues[i]->rx.stats.ll_polls;
		data += NUM_STATS_PER_RX_QUEUE;
	}
}

static void sn_ethtool_get_drvinfo(struct net_device *netdev,
                              struct ethtool_drvinfo *drvinfo)
{
	strcpy(drvinfo->driver, "BESS");
	strcpy(drvinfo->version, "99.9.9");
	strcpy(drvinfo->bus_info, "PCIe Gen 7");

	drvinfo->regdump_len = 0;
	drvinfo->eedump_len = 0;
}

const struct ethtool_ops sn_ethtool_ops = {
	.get_sset_count 	= sn_ethtool_get_sset_count,
	.get_strings 		= sn_ethtool_get_strings,
	.get_ethtool_stats 	= sn_ethtool_get_ethtool_stats,
	.get_drvinfo		= sn_ethtool_get_drvinfo,
};
