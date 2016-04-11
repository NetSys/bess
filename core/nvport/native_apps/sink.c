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
struct sn_port *in_port;
struct sn_port *out_port; /* UNUSED in sink */

// Print stats to stdout?
int print_stats = 1;

int batch_size = 1;

struct {
	uint64_t rx_pkts;
	uint64_t rx_batch;
	uint64_t rx_bytes;

	uint64_t tx_pkts;
	uint64_t tx_batch;
	uint64_t tx_bytes;
} stats, last_stats;

char unique_name[APPNAMESIZ];

static int run_sink(void)
{
	int ret = 0;

	uint64_t hz = rte_get_tsc_hz();

	struct snbuf *pkts[batch_size];
	int received;

	int i;
	int rxq;

	for (rxq = 0; rxq < in_port->num_txq; rxq++) {
		received = sn_receive_pkts(in_port, rxq, pkts, batch_size);

		if (received == 0)
			continue;

		uint64_t now = rte_rdtsc();

		char *buf = rte_pktmbuf_mtod(&pkts[0]->mbuf, char *);
		uint64_t *payload = (uint64_t *)(buf + 48);
		uint64_t latency_cycles = now - *payload;

		printf("%f\n", latency_cycles * 1000000.0 / hz);

		stats.rx_pkts += received;
		stats.rx_batch++;
		stats.rx_bytes += received * 24;	/* Ethernet overheads */

		for (i = 0; i < received; i++) {
			stats.rx_bytes += snb_total_len(pkts[i]);

			/* Slow since packets are freed on this (remote) core */
			snb_free(pkts[i]);	
		}

		ret += received;
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

	memset(in_ifname, 0, sizeof(in_ifname));
	memset(out_ifname, 0, sizeof(out_ifname));

	printf("Launched!\n");

	int opt;
    
	while ((opt = getopt(argc, argv, "c:i:o:n:")) != -1) {
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

	// Main run loop
	for (;; loop_count++) {

		int idle = 1;

		if (run_sink() > 0)
			idle = 0;

		if (idle)
			idle_count++;

		if ((loop_count % 100) || rte_rdtsc() - last_tsc < hz)
			continue;

		if (print_stats)
			emit_stats(loop_count, idle_count);



		memcpy(&last_stats, &stats, sizeof(stats));
		loop_count = 0;
		idle_count = 0;
		last_tsc = rte_rdtsc();
	}

	return 0;
}
