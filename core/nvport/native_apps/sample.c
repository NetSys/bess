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

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include <rte_config.h>
#include <rte_cycles.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_byteorder.h>

#include "sn.h"

enum {
	MODE_SINK,
	MODE_SOURCE,
	MODE_ECHO,
} mode = MODE_ECHO;

#define MAX_PORTS	16
struct sn_port *ports[MAX_PORTS];
int num_ports = 0;

int dump = 0;
int pkt_size = 64;
int batch_size = 32;

struct {
	uint64_t rx_pkts;
	uint64_t rx_batch;
	uint64_t rx_bytes;

	uint64_t tx_pkts;
	uint64_t tx_batch;
	uint64_t tx_bytes;
} stats, last_stats;

static int run_sink(struct sn_port *port)
{
	int ret = 0;

	struct snbuf *pkts[batch_size];
	int received;

	int i;
	int rxq;

	for (rxq = 0; rxq < port->num_txq; rxq++) {
		received = sn_receive_pkts(port, rxq, pkts, batch_size);

		if (received == 0)
			continue;

		stats.rx_pkts += received;
		stats.rx_batch++;
		stats.rx_bytes += received * 24;	/* Ethernet overheads */

		if (unlikely(dump)) {
			for (i = 0; i < received; i++)
				rte_pktmbuf_dump(stdout, (struct rte_mbuf*)pkts[i], 64);
		}

		for (i = 0; i < received; i++) {
			stats.rx_bytes += snb_total_len(pkts[i]);

			/* Slow since packets are freed on this (remote) core */
			sn_snb_free(pkts[i]);
		}

		ret += received;
	}

	return ret;
}

/* Copied from a DPDK example */
static void update_ip_csum(struct ipv4_hdr *ip)
{
	uint32_t ip_cksum;
	uint16_t *buf;

	buf = (uint16_t *) ip;
	ip_cksum = 0;
	ip_cksum += buf[0];
	ip_cksum += buf[1];
	ip_cksum += buf[2];
	ip_cksum += buf[3];
	ip_cksum += buf[4];
	/* buf[5]: checksum field */
	ip_cksum += buf[6];
	ip_cksum += buf[7];
	ip_cksum += buf[8];
	ip_cksum += buf[9];

	/* reduce16 */
	ip_cksum = ((ip_cksum & 0xFFFF0000) >> 16) + (ip_cksum & 0x0000FFFF);
	if (ip_cksum > 65535)
		ip_cksum -= 65535;
	ip_cksum = (~ip_cksum) & 0x0000FFFF;
	if (ip_cksum == 0)
		ip_cksum = 0xFFFF;

	ip->hdr_checksum = (uint16_t) ip_cksum;
}

#define SET_LLADDR(lladdr, a, b, c, d, e, f) \
		do { \
			(lladdr).addr_bytes[0] = a; \
			(lladdr).addr_bytes[1] = b; \
			(lladdr).addr_bytes[2] = c; \
			(lladdr).addr_bytes[3] = d; \
			(lladdr).addr_bytes[4] = e; \
			(lladdr).addr_bytes[5] = f; \
		} while (0)

/* build a UDP packet with dummy contents */
static void build_packet(char *buf, int size)
{
	struct ether_hdr *eth;
	struct ipv4_hdr *ip;
	struct udp_hdr *udp;

	/* build an ethernet header */
	eth = (struct ether_hdr *)buf;

	SET_LLADDR(eth->d_addr, 0, 0, 0, 0, 0, 2);
	SET_LLADDR(eth->s_addr, 0, 0, 0, 0, 0, 1);
	eth->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

	/* build an IP header */
	ip = (struct ipv4_hdr *)((char *)eth + sizeof(*eth));

	ip->version_ihl 	= (4 << 4) | sizeof(struct ipv4_hdr) >> 2;
	ip->type_of_service 	= 0;
	ip->total_length 	= rte_cpu_to_be_16(size - sizeof(*eth));
	ip->packet_id 		= rte_cpu_to_be_16(0);
	ip->fragment_offset 	= rte_cpu_to_be_16(0);
	ip->time_to_live 	= 64;
	ip->next_proto_id 	= IPPROTO_UDP;
	ip->dst_addr 		= rte_cpu_to_be_32(IPv4(192, 168, 0, 2));
	ip->src_addr 		= rte_cpu_to_be_32(IPv4(192, 168, 0, 1));
	update_ip_csum(ip);

	/* build a UDP header */
	udp = (struct udp_hdr *)((char *)ip + sizeof(*ip));

	udp->src_port 		= rte_cpu_to_be_16(1234);
	udp->dst_port 		= rte_cpu_to_be_16(5678);
	udp->dgram_len 		= rte_cpu_to_be_16(size - sizeof(*eth) - sizeof(*ip));
	udp->dgram_cksum 	= rte_cpu_to_be_16(0);
}

static int run_source(struct sn_port *port)
{
	int ret = 0;

	struct snbuf *pkts[batch_size];
	int sent;

	int i;
	int txq;

	for (txq = 0; txq < port->num_rxq; txq++) {
		for (i = 0; i < batch_size; i++) {
			char *buf;

			pkts[i] = sn_snb_alloc();
			assert(pkts[i]);

			buf = snb_append(pkts[i], pkt_size);
			assert(buf);
			build_packet(buf, pkt_size);
		}

		sent = sn_send_pkts(port, txq, pkts, batch_size);

		stats.tx_pkts += sent;
		stats.tx_batch += (sent > 0);
		stats.tx_bytes += sent * 24;	/* Ethernet overheads */

		for (i = 0; i < sent; i++) {
			/* NOTE: accessing packets after they are sent is dangerous.
			 * (they may have been already freed on BESS cores
			 * Don't try this at home */
			stats.tx_bytes += snb_total_len(pkts[i]);
		}

		/* free unsent packets */
		for (i = sent; i < batch_size; i++) {
			/* Slow since packets are freed on this (remote) core */
			sn_snb_free(pkts[i]);
		}

		ret += sent;
	}

	return ret;
}

static int run_echo(struct sn_port *port)
{
	int ret = 0;

	struct snbuf *pkts[batch_size];
	int received;
	int sent;

	int i;
	int rxq;

	for (rxq = 0; rxq < port->num_txq; rxq++) {
		received = sn_receive_pkts(port, rxq, pkts, batch_size);

		if (received == 0)
			continue;

		stats.rx_pkts += received;
		stats.rx_batch += (received > 0);
		stats.rx_bytes += received * 24;	/* Ethernet overheads */

		if (unlikely(dump)) {
			for (i = 0; i < received; i++)
				rte_pktmbuf_dump(stdout, (struct rte_mbuf*)pkts[i], 64);
		}

		for (i = 0; i < received; i++)
			stats.rx_bytes += snb_total_len(pkts[i]);

		sent = sn_send_pkts(port, rxq % port->num_rxq, pkts, received);

		stats.tx_pkts += sent;
		stats.tx_batch++;
		stats.tx_bytes += sent * 24;	/* Ethernet overheads */

		for (i = 0; i < sent; i++) {
			/* NOTE: accessing packets after they are sent is dangerous.
			 * (they may have been already freed on BESS cores
			 * Don't try this at home */
			stats.tx_bytes += snb_total_len(pkts[i]);
		}

		/* free unsent packets */
		for (i = sent; i < received; i++) {
			/* Slow since packets are freed on this (remote) core */
			sn_snb_free(pkts[i]);
		}

		ret += received;
		ret += sent;
	}

	return ret;
}

void show_usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s [-c <core id>] [-m source|sink|echo] "
			"[-p <packet size>] [-b <batch size>]\n",
			prog_name);

	exit(1);
}

void init_ports(void)
{
	for (num_ports = 0; num_ports < MAX_PORTS; num_ports++) {
		char ifname[256];

		sprintf(ifname, "vport%d", num_ports);
		ports[num_ports] = init_port(ifname);

		if (!ports[num_ports])
			break;
	}

	assert(num_ports > 0);
}

int main(int argc, char **argv)
{
	uint64_t last_tsc;
	uint64_t hz;

	uint64_t loop_count = 0;
	uint64_t idle_count = 0;

	uint64_t core = 7;

	int opt;

	int (*func)(struct sn_port *);

	while ((opt = getopt(argc, argv, "c:m:p:b:")) != -1) {
		switch (opt) {
		case 'c':
			core = atoi(optarg);
			break;
		case 'm':
			if (strcmp(optarg, "source") == 0)
				mode = MODE_SOURCE;
			else if (strcmp(optarg, "sink") == 0)
				mode = MODE_SINK;
			else if (strcmp(optarg, "echo") == 0)
				mode = MODE_ECHO;
			else
				show_usage(argv[0]);
			break;
		case 'p':
			pkt_size = atoi(optarg);
			assert(60 <= pkt_size && pkt_size <= 1518);
			break;
		case 'b':
			batch_size = atoi(optarg);
			assert(1 <= batch_size && batch_size <= 32);
			break;
		default:
			show_usage(argv[0]);
		}
	}

	init_bess(1 << core, "sample");
	init_ports();

	printf("%d ports found\n", num_ports);

	switch (mode) {
	case MODE_SINK:
		printf("Running in sink mode\n");
		func = run_sink;
		break;
	case MODE_SOURCE:
		printf("Running in source mode: packet size=%d\n", pkt_size);
		func = run_source;
		break;
	case MODE_ECHO:
		printf("Running in echo mode\n");
		func = run_echo;
		break;
	default:
		assert(0);
	}

	printf("Packet dump %s for RX\n", dump ? "enabled" : "disabled");
	printf("Batch size: %d\n", batch_size);

	hz = rte_get_tsc_hz();
	last_tsc = rte_rdtsc();

	for (;; loop_count++) {
		int idle = 1;
		int i;

		for (i = 0; i < num_ports; i++) {
			if (func(ports[i]) > 0)
				idle = 0;
		}

		if (idle)
			idle_count++;

		if ((loop_count % 100) || rte_rdtsc() - last_tsc < hz)
			continue;

		printf("Idle: %4.1f%%\t\t"
		       "RX: %8lu pkts/s (%4.1f pkts/batch) %7.1f Mbps\t\t"
		       "TX: %8lu pkts/s (%4.1f pkts/batch) %7.1f Mbps\n",
				(double)idle_count * 100 / loop_count,
				stats.rx_pkts - last_stats.rx_pkts,
				(double)(stats.rx_pkts - last_stats.rx_pkts) /
					((stats.rx_batch - last_stats.rx_batch) ? : 1),
				(double)(stats.rx_bytes - last_stats.rx_bytes)
					* 8 / 1000000,
				stats.tx_pkts - last_stats.tx_pkts,
				(double)(stats.tx_pkts - last_stats.tx_pkts) /
					((stats.tx_batch - last_stats.tx_batch) ? : 1),
				(double)(stats.tx_bytes - last_stats.tx_bytes)
					* 8 / 1000000
				);

		memcpy(&last_stats, &stats, sizeof(stats));
		loop_count = 0;
		idle_count = 0;
		last_tsc = rte_rdtsc();
	}

	return 0;
}
