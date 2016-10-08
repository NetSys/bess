#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sched.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <rte_config.h>
#include <rte_cycles.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_byteorder.h>
#include <rte_lpm.h>

#include <time.h>
#include <stdio.h>

#include "sn.h"

static uint64_t hz;

// Ports
struct sn_port *in_port;
struct sn_port *out_port;

// Cycles to stall for
uint64_t stalled_cycles = 0;

// Print stats to stdout?
int print_stats = 0;

int batch_size = 32;

int yield = 0;

int statistics = 0;

int memory_access = 0;

int fake_core = -1;

struct rte_lpm* lpm = NULL;

int lookup = 0;

int touch = 0;

int copy = 0;

uint64_t memory_access_size = 0;
struct {
	uint64_t rx_pkts;
	uint64_t rx_batch;
	uint64_t rx_bytes;

	uint64_t tx_pkts;
	uint64_t tx_batch;
	uint64_t tx_bytes;

	uint32_t csum;
} stats, last_stats;

char unique_name[APPNAMESIZ];

uint64_t *memory_access_region;

uint64_t memory_access_accum = 0;

#define is_power_of_2(x)        ((x) != 0 && (((x) & ((x) - 1)) == 0))

static inline uint32_t rand_fast(uint64_t* seed)
{
	*seed = *seed * 1103515245 + 12345;
	return (*seed) >> 32;
}

static inline void do_work(struct snbuf *pkt)
{
	int i;
	/* Figure out how to get rte_lpm to work with static linking */
	if (lookup) {
		struct ipv4_hdr *ip = (struct ipv4_hdr*)(snb_head_data(pkt) +
						 sizeof(struct ether_hdr));
		uint32_t nextHop = 0;
		int ret = rte_lpm_lookup(lpm, ip->src_addr, &nextHop);
		assert(ret == 0 || ret == -ENOENT);
		if (ret == 0) {
			stats.csum += nextHop;
		}
	}

	if (touch) {
		struct ipv4_hdr *ip =
			(struct ipv4_hdr*)(snb_head_data(pkt) +
					sizeof(struct ether_hdr));
		
		stats.csum += ip->src_addr;
	}
	if (stalled_cycles) {
		uint64_t start, end;
		start = rte_rdtsc();
		end = start;
		while (end - start < stalled_cycles) {
			end = rte_rdtsc();
		}
	}
	if (memory_access) {
		uint64_t accum = 0;
		static uint64_t seed = 0;
		for (i = 0; i < memory_access; i++) {
			uint32_t offset = rand_fast(&seed) &
				((memory_access_size >> 3) - 1);
			accum += memory_access_region[offset];
		}
		memory_access_accum += accum;
	}
}

static inline int recv_pkts(struct sn_port *port,
		int qid, struct snbuf **pkts, int batch_size)
{
	int received, i;
	received = sn_receive_pkts(port, qid, pkts, batch_size);

	if (statistics) {
		stats.rx_pkts += received;
		stats.rx_batch += (received > 0);
		stats.rx_bytes += received * 24;	/* Ethernet overheads */

		for (i = 0; i < received; i++)
			stats.rx_bytes += snb_total_len(pkts[i]);
	}
	
	return received;
}

static inline int send_pkts(struct sn_port *port, int qid,
		struct snbuf **pkts, int cnt)
{

	int sent;
	int i;
	struct snbuf *sndPkts[batch_size];
	if (copy) {
		sn_snb_copy_batch(pkts, sndPkts, cnt);
		sent = sn_send_pkts(port, qid % port->num_txq, sndPkts, cnt);
		sn_snb_free_bulk(pkts, cnt);
		/* free unsent packets */
		for (i = sent; i < cnt; i++) {
			/* Slow since packets are freed on this (remote) core */
			snb_free(sndPkts[i]);
			sndPkts[i] = NULL;
		}
	} else {
		sent = sn_send_pkts(port, qid % port->num_txq, pkts, cnt);
		/*free unsent packets*/
		for (i = sent; i < cnt; i++) {
			/* slow since packets are freed on this (remote) core */
			snb_free(pkts[i]);
			pkts[i] = NULL;
		}
	}
	return sent;
}


static inline int run_fastforward(void)
{
	int ret = 0;

	struct snbuf *pkts[batch_size];

	int received;
	int sent;

	int i;
	int rxq;

	for (rxq = 0; rxq < in_port->num_rxq; rxq++) {
		received = recv_pkts(in_port, rxq, pkts, batch_size);

		if (received == 0) {
			if (yield) {
				sched_yield();
			}
			continue;
		}

		for (i = 0; i < received; i++) {
			do_work(pkts[i]);
		}
		
		sent = send_pkts(out_port, rxq, pkts, received);

		stats.tx_pkts += sent;
		stats.tx_batch++;


		ret += received;
		ret += sent;
	}

	return ret;
}

static int interrupt_cnt = 0;

static inline int run_fastforward_event(int efd)
{
	int ret = 0;

	struct snbuf *pkts[batch_size];
	int received;
	int sent;

	int i;
	int rxq = 0;

	int work_done = 0;
	int no_recv_cnt = 0;
	struct epoll_event evs[1024];
	/*int k;*/

	//for (rxq = 0; rxq < in_port->num_rxq; rxq++) {
	while (no_recv_cnt < in_port->num_rxq) {
		received = recv_pkts(in_port, rxq, pkts, batch_size);
		while (received) {
			no_recv_cnt = 0;
			
			for (i = 0; i < received; i++) {
				do_work(pkts[i]);
			}
			sent = send_pkts(out_port, rxq, pkts, received);

			stats.tx_pkts += sent;
			stats.tx_batch++;

			ret += received;
			ret += sent;

			work_done++;

			if (work_done % 10 == 0) {
				sched_yield();
				break;
			}

			rxq = (rxq + 1) % in_port->num_rxq;			
			received = recv_pkts(in_port, rxq, pkts, batch_size);
		}
		rxq = (rxq + 1) % in_port->num_rxq;		
		no_recv_cnt++;
	}

	if (!work_done) {
		uint64_t loop_start = rte_rdtsc();

		/* 10us interrupt mitigation */
		while ((rte_rdtsc() - loop_start) * 1000000000.0 / hz < 10000.0 ) {
			for (rxq = 0; rxq < in_port->num_rxq; rxq++)
				if (!llring_empty(in_port->rx_qs[rxq]))
					return ret;
		}
		
		for (rxq = 0; rxq < in_port->num_rxq; rxq++)
			sn_enable_interrupt(in_port->rx_regs[rxq]);

		for (rxq = 0; rxq < in_port->num_rxq; rxq++)
			if (!llring_empty(in_port->rx_qs[rxq]))
				return ret;

		int n = epoll_wait(efd, evs, 1024, -1);

		for (i = 0; i < n; i++) {
			char buf[1024];

			rxq = evs[i].data.u32;
			interrupt_cnt +=
				read(in_port->fd[rxq], buf, sizeof(buf));
		}
	}

	return ret;
}

void show_usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s -i <input iface> -o <output iface>"
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

	uint64_t loop_count = 0;

	uint64_t core = 7;

	uint64_t polling = 1;

	int socket_id = 0;

	int max_rules = 16635;

	char in_ifname[IFNAMSIZ];
	char out_ifname[IFNAMSIZ];
	char *rib_file = NULL;

	int efd;
	char *endptr;

	memset(in_ifname, 0, sizeof(in_ifname));
	memset(out_ifname, 0, sizeof(out_ifname));

	printf("Launched!\n");

	int opt;

	while ((opt = getopt(argc, argv, "a:c:i:o:r:n:m:w:v:x:z:peysfdltq"))
			!= -1) {
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
		case 'm':
			max_rules = (int)strtoul(optarg, &endptr, 10);
			if (endptr == optarg) {
				show_usage(argv[0]);
				exit(1);
			}
			break;
		case 'r':
			rib_file = optarg;
			break;
		case 'l':
			lookup = 1;
			break;
		case 't':
			touch = 1;
			break;
		case 'w':
			stalled_cycles = strtoul(optarg, &endptr, 10);
			if (endptr == optarg) {
				show_usage(argv[0]);
				exit(1);
			}
			break;
		case 'v':
			fake_core = atoi(optarg);
			break;
		case 'x':
			memory_access = atoi(optarg);
			break;
		case 'z':
			memory_access_size = atoll(optarg);
			break;
		case 'q':
			copy = 1;
			break;
		default:
			show_usage(argv[0]);
		}
	}

	if (lookup && !rib_file) {
		printf("Need RIB file, exiting\n");
		return 0;
	}
	if (!unique_name[0]) {
		// Choose a random unique name if one isn't provided
		snprintf(unique_name, sizeof(unique_name), "%u", rand());
	}
	if (fake_core == -1) {
		fake_core = core;
	}
	init_bess(core, unique_name);
	RTE_PER_LCORE(_lcore_id) = fake_core;

	printf("Started iso_test with unique name %s\n", unique_name);
	printf("registering input port %s\n", in_ifname);
	printf("registering output port %s\n", out_ifname);

	
	if (lookup) {
		struct rte_lpm_config config = {
			.max_rules = max_rules,
			.number_tbl8s = 1024,
		};

		printf("RIB file is %s\n", rib_file);
		lpm = rte_lpm_create(unique_name, socket_id, &config);
		assert(lpm);
		FILE* rib = fopen(rib_file, "r");

		assert(rib);
		char *rule = NULL;
		size_t len = 0;
		int count = 0;
		while (getline(&rule, &len, rib) > 0) {
			assert(count < max_rules);
			char* ipPart = strtok(rule, " ");
			char* dest = strtok(NULL, " ");
			char* ip = strtok(ipPart, "/");
			char* len = strtok(NULL, "/");
			if (ipPart == NULL || dest == NULL ||
					ip == NULL || len == NULL) {
				continue;
			}
			uint32_t ipInt = 0;
			int convert = inet_pton(AF_INET, ip, &ipInt);
			if (!convert) {
				printf("Error converting IP address %s\n", ip);
			}
			ipInt = ntohl(ipInt);
			// rte_lpm returns 8 bit dest
			rte_lpm_add (lpm, ipInt, atoi(len),
					(atoi(dest) & 0xff));
			count++;
		}
		fclose(rib);
		printf("Done reading %d rules\n", count);
		printf("Read a total of %d rules\n", count);
	}

	init_sn_port(in_ifname, 1);
	init_sn_port(out_ifname, 0);

	hz = rte_get_tsc_hz();
	last_tsc = rte_rdtsc();

	if (!polling) {
		struct epoll_event ev;
		int rxq;
		efd = epoll_create(1024);
		
		for (rxq = 0; rxq < in_port->num_rxq; rxq++) {
			ev.events = EPOLLIN;
			ev.data.u32 = rxq;
			epoll_ctl(efd, EPOLL_CTL_ADD,  in_port->fd[rxq], &ev);
		}
	}

	if (memory_access) {
		//int i;
		memory_access = memory_access;
		memory_access_region = malloc(memory_access_size);
		assert(memory_access_region != NULL);
		assert(memory_access_size >= 8);
		assert(is_power_of_2(memory_access_size));
		memset(memory_access_region, 0, memory_access_size);
		//for (i = 0; i < memory_access_size / sizeof(uint64_t); i++)
		//	memory_access_region[i] = i;
		
		printf("memory_access_size %ld\n", memory_access_size);
	}


	// Main run loop
	for (;; loop_count++) {

		if (polling) {
			run_fastforward();
		} else {
			run_fastforward_event(efd);
		}
	}

	return 0;
}
