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

#include <time.h>

#include "sn.h"

// Ports
struct sn_port *in_port;        /** UNUSED in source */
struct sn_port *out_port;

// Print stats to stdout?
int print_stats = 0;

int batch_size = 1;

int pkt_size = 64;

struct {
	uint64_t rx_pkts;
	uint64_t rx_batch;
	uint64_t rx_bytes;

	uint64_t tx_pkts;
	uint64_t tx_batch;
	uint64_t tx_bytes;
} stats, last_stats;

char unique_name[APPNAMESIZ];

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

#define SET_LLADDR(lladdr, a, b, c, d, e, f)	\
	do {					\
		(lladdr).addr_bytes[0] = a;	\
		(lladdr).addr_bytes[1] = b;	\
		(lladdr).addr_bytes[2] = c;	\
		(lladdr).addr_bytes[3] = d;	\
		(lladdr).addr_bytes[4] = e;	\
		(lladdr).addr_bytes[5] = f;	\
	} while (0)

static char pkt_tmp[64];
static struct ether_hdr *eth_tmp;
static struct ipv4_hdr *ip_tmp;
static struct udp_hdr *udp_tmp;

static void build_template(int size)
{
	eth_tmp = (struct ether_hdr*)pkt_tmp;
	ip_tmp  = (struct ipv4_hdr*)(pkt_tmp + sizeof(struct ether_hdr));
	udp_tmp = (struct udp_hdr*)(pkt_tmp + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));

	SET_LLADDR(eth_tmp->d_addr, 0, 0, 0, 0, 0, 2);
	SET_LLADDR(eth_tmp->s_addr, 0, 0, 0, 0, 0, 1);
	eth_tmp->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

	ip_tmp->version_ihl 	= (4 << 4) | sizeof(struct ipv4_hdr) >> 2;
	ip_tmp->type_of_service	= 0;
	ip_tmp->total_length 	= rte_cpu_to_be_16(size - sizeof(*eth_tmp));
	ip_tmp->packet_id      	= rte_cpu_to_be_16(0);
	ip_tmp->fragment_offset	= rte_cpu_to_be_16(0);
	ip_tmp->time_to_live 	= 64;
	ip_tmp->next_proto_id 	= IPPROTO_UDP;
	ip_tmp->dst_addr       	= rte_cpu_to_be_32(IPv4(192, 168, 0, 2));
	ip_tmp->src_addr       	= rte_cpu_to_be_32(IPv4(192, 168, 0, 1));
	update_ip_csum(ip_tmp);

	udp_tmp->src_port      	= rte_cpu_to_be_16(1234);
	udp_tmp->dst_port      	= rte_cpu_to_be_16(5678);
	udp_tmp->dgram_len    	= rte_cpu_to_be_16(size - sizeof(*eth_tmp) - sizeof(*ip_tmp));
	udp_tmp->dgram_cksum 	= rte_cpu_to_be_16(0);
}

/* build a UDP packet with dummy contents */
static void build_packet(char *buf, int size)
{
	memcpy(buf, pkt_tmp, 48);

	uint64_t *payload = (uint64_t *)(buf + 48);
	*payload = rte_rdtsc();
}

static int run_source(void)
{
	int ret = 0;

	struct snbuf *pkts[batch_size];
	int sent;

	int i;
	int txq;

	for (txq = 0; txq < out_port->num_rxq; txq++) {
		for (i = 0; i < batch_size; i++) {
			char *buf;

			pkts[i] = sn_snb_alloc();
			assert(pkts[i]);

			buf = snb_append(pkts[i], pkt_size);
			assert(buf);
			build_packet(buf, pkt_size);
		}


		sent = sn_send_pkts(out_port, txq, pkts, batch_size);

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

void show_usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s -i <input iface> -o <output iface>"
		"[-r <rate mbps>] "
		"[-c <core id>] [-p <packet size>] [-b <batch size>]\n",
		prog_name);
	exit(1);
}

/** Initialize a port. If inc is 1, the port is set to the global
 * incoming port. Otherwise, it's set to the outgoing port. */
void init_sn_port(const char *ifname, int inport)
{
	struct sn_port *p = init_port(ifname);
	assert(p);

	if (inport)
		in_port = p;
	else
		out_port = p;
}

void emit_stats(uint64_t loop_count, uint64_t idle_count)
{
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
}

int main(int argc, char **argv)
{
	uint64_t last_tsc;
	uint64_t hz;

	uint64_t loop_count = 0;
	uint64_t idle_count = 0;

	// eh..how do i handle this.
	uint64_t core = 7;

	char in_ifname[IFNAMSIZ];
	char out_ifname[IFNAMSIZ];

	char *endptr;

	memset(in_ifname, 0, sizeof(in_ifname));
	memset(out_ifname, 0, sizeof(out_ifname));

	printf("Launched!\n");


	int opt;

	while ((opt = getopt(argc, argv, "c:i:o:n:s:")) != -1) {
		switch (opt) {
		case 'c':
			core = atoi(optarg);
			break;
		case 'i':
			strncpy(in_ifname, optarg, IFNAMSIZ);
			break;
		case 'o':
			strncpy(out_ifname, optarg, IFNAMSIZ);
			break;
		case 'n':
			strncpy(unique_name, optarg, APPNAMESIZ);
			break;
		case 's':
			pkt_size = strtoul(optarg, &endptr, 10);
			assert(optarg != endptr);
			assert(pkt_size >= 60);
			assert(pkt_size <= 1514);
			break;
		default:
			show_usage(argv[0]);
		}
	}

	if (!unique_name[0]) {
		// Choose a random unique name if one isn't provided
		snprintf(unique_name, sizeof(unique_name), "%u", rand());
	}
	init_bess(1 << core, unique_name);

	printf("Started fastforward with unique name %s\n", unique_name);
	printf("registering input port %s\n", in_ifname);
	printf("registering output port %s\n", out_ifname);


	init_sn_port(in_ifname, 1);
	init_sn_port(out_ifname, 0);

	hz = rte_get_tsc_hz();
	last_tsc = rte_rdtsc();

	build_template(pkt_size);
	// Main run loop
	for (;; loop_count++) {

		int idle = 1;
		if (run_source() > 0)
			idle = 0;

		sleep(1);

		if (idle)
			idle_count++;

		if ((loop_count % 100) || rte_rdtsc() - last_tsc < hz)
			continue;

		if (print_stats)
			emit_stats(loop_count, idle_count);

		loop_count = 0;
		idle_count = 0;
		last_tsc = rte_rdtsc();
	}

	return 0;
}
