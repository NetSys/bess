#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sched.h>

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

struct sn_port *in_port;
struct sn_port *out_port;

int core = 1;
int batch_size = 32;
int yield = 0;
int statistics = 0;
int pkt_size = 60;
int print_stats = 0;
int polling = 1;

struct {
	uint64_t rx_pkts;
	uint64_t rx_batch;
	uint64_t rx_bytes;

	uint64_t tx_pkts;
	uint64_t tx_batch;
	uint64_t tx_bytes;
} stats, last_stats;



static int run_source(void)
{
	int sent, sent_total = 0;
	int txq;
	int i;
	struct snbuf *pkts[batch_size];

	for (txq = 0; txq < out_port->num_rxq; txq++) {
		sn_snb_alloc_bulk(pkts, batch_size);
		
		for (i = 0; i < batch_size; i++)
			pkts[i]->mbuf.pkt_len =
				pkts[i]->mbuf.data_len = pkt_size;
		
		sent = sn_send_pkts(out_port, txq, pkts, batch_size);
		sent_total += sent;

		if (statistics) {
			stats.tx_pkts += sent;
			stats.tx_batch += (sent > 0);
			stats.tx_bytes += sent * (pkt_size + 24);
		}
		
		if (batch_size - sent > 0)
			sn_snb_free_bulk(pkts + sent, batch_size - sent);
	}

	return sent_total;
}


static int run_sink(void)
{
	struct snbuf *pkts[batch_size];
	int received, recv_total = 0;

	int i;
	int rxq;

	for (rxq = 0; rxq < in_port->num_txq; rxq++) {
		received = sn_receive_pkts(in_port, rxq, pkts, batch_size);
		if (received == 0)
			continue;

		recv_total += received;
		
		if (statistics) {
			stats.rx_pkts += received;
			stats.rx_batch += (received > 0);
			stats.rx_bytes += received * 24;	/* Ethernet overheads */

			for (i = 0; i < received; i++)
				stats.rx_bytes += snb_total_len(pkts[i]);
		}

		sn_snb_free_bulk(pkts, received);
	}

	return recv_total;
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

void show_usage(char *cmd)
{
}

int main(int argc, char **argv)
{
	char in_ifname[IFNAMSIZ] = {0};
	char out_ifname[IFNAMSIZ] ={0};
	char unique_name[APPNAMESIZ];
	
	int opt;

	while ((opt = getopt(argc, argv, "c:i:o:n:peys")) != -1) {
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
		case 'p':
			print_stats = 1;
			break;
		case 'e':
			polling = 0;
			break;
		case 'y':
			yield = 1;
			break;
		case 's':
			statistics = 1;
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

	printf("Starting sourcesink with unique name %s\n", unique_name);

	if (strlen(in_ifname) > 0) {
		printf("sink input port %s\n", in_ifname);		
		in_port = init_port(in_ifname);
	}
	
	if (strlen(out_ifname) > 0) {
		if (strncmp(in_ifname, out_ifname, IFNAMSIZ) != 0) {
			printf("source output port %s\n", out_ifname);
			out_port = init_port(out_ifname);
		} else {
			out_port = in_port;
		}
	}
	
	for (;;) {
		run_source();
		if (run_sink() == 0)
			sched_yield();
			
	}
}
