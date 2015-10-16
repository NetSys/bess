#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <rte_launch.h>

#include "debug.h"
#include "dpdk.h"
#include "master.h"
#include "worker.h"
#include "driver.h"
#include "syslog.h"

#if 0
#include "old_modules/vport_guest_sndrv.h"
#include "old_modules/switch.h"
#include "old_modules/ratelimiter.h"

#define RATELIMITER 1
#define TIMESTAMP 0

static uint64_t test_rate = 1000000;
static int test_num_txq = 1;
static int test_num_rxq = 1;
static int test_num_pports = 1;
static int test_num_vports = 1;

void parse_test_args(int argc, char **argv)
{
	char c;
	while (argc > 0)  {
		if (strncmp(argv[0], "--", 2) == 0)
			break;
		argv++;
		argc--;
	}

	while ((c = getopt(argc, argv, "p:v:b:t:r:")) != -1) {
		switch(c) {
		case 'p':
			test_num_pports = atoi(optarg);
			break;
		case 'v':
			test_num_vports = atoi(optarg);
			break;
		case 't':
			test_num_txq = atoi(optarg);
			break;
		case 'r':
			test_num_rxq = atoi(optarg);
			break;
		case 'b':
			test_rate = atol(optarg);
			printf("rate %ldmbps\n", test_rate / 1000000);
			break;
		default:
			assert(0);
		}

	}
}

static void add_pipeline_p_to_sw(struct pport *p, struct module *sw)
{
	char name[MODULE_NAME_LEN];

	struct module *p_inc;
	struct module *parse;
	struct module *checksum;
#if TIMESTAMP
	struct module *timestamp_rx;
#endif


	sprintf(name, "pport%hhu_inc", p->port.port_id);
	p_inc = init_module(name, &pport_inc_ops, 1, p);

	parse = init_module("parse", &parse_ops, 0, NULL);

	checksum = init_module("checksum_rx", &checksum_rx_ops, 0, NULL);
#if TIMESTAMP
	timestamp_rx = init_module("timestamp_rx", &timestamp_rx_ops, 0, NULL);
#endif
	set_next_module(p_inc, parse);
	
#if TIMESTAMP
	set_next_module(parse, timestamp_rx);
	set_next_module(timestamp_rx, checksum);
#else
	set_next_module(parse, checksum);
#endif
	set_next_module(checksum, sw);
}

static void add_pipeline_sw_to_p(struct module *sw, struct pport *p)
{
	char name[MODULE_NAME_LEN];

	struct module *tso;
	struct module *checksum;
	struct module *ratelimiter;
	struct module *p_out;

	tso = init_module("tso", &tso_ops, 0, NULL);

	checksum = init_module("checksum_tx", &checksum_tx_ops, 0, NULL);

	ratelimiter = init_module("ratelimiter", &ratelimiter_tx_ops, 1 , p);

	sprintf(name, "pport%hhu_out", p->port.port_id);
	p_out = init_module(name, &pport_out_ops, 0, p);

	add_next_module(sw, tso, &p->port);
	set_next_module(tso, checksum);
	set_next_module(checksum, ratelimiter);
	set_next_module(ratelimiter, p_out);
}

static void add_pipeline_v_to_sw(struct vport *v, struct module *sw)
{
	char name[MODULE_NAME_LEN];

	struct module *v_inc;
	struct module *parse;
	struct module *flowsteer;

	sprintf(name, "vport%hhu_inc", v->port.port_id);
	v_inc = init_module(name, &vport_inc_ops, 1, v);

	parse = init_module("parse", &parse_ops, 0, NULL);

	sprintf(name, "flowsteer_tx:%hhu", v->port.port_id);
	flowsteer = init_module(name, &flowsteer_tx_ops, 0, v);

	set_next_module(v_inc, parse);
	set_next_module(parse, flowsteer);
	set_next_module(flowsteer, sw);
}

static void add_pipeline_sw_to_v(struct module *sw, struct vport *v)
{
	char name[MODULE_NAME_LEN];

	struct module *lro;
	struct module *flowsteer;
	struct module *v_out;

	lro = init_module("lro", &lro_ops, 0, NULL);

	sprintf(name, "flowsteer_rx:%hhu", v->port.port_id);
	flowsteer = init_module(name, &flowsteer_rx_ops, 0, v);

	sprintf(name, "vport%hhu_out", v->port.port_id);
	v_out = init_module(name, &vport_out_ops, 0, v);

	add_next_module(sw, lro, &v->port);
	set_next_module(lro, flowsteer);
	set_next_module(flowsteer, v_out);
}

static void test_many_pports()
{
	struct pport *pport;

	struct vport *vport;
	struct vport_config conf;

	struct module *sw;

	const int num_pports = 1;
	int i;

	struct miniflow_entry entry;

	sw = init_module("sw_bridge", &sw_bridge_ops, 0, NULL);

	/* physical ports (should be set earlier than vports) */
	for (i = 0; i < num_pports; i++) {
		pport = init_pport(i, num_workers, num_workers);

		add_pipeline_p_to_sw(pport, sw);
		add_pipeline_sw_to_p(sw, pport);
	}

	/* virtual ports */
	strcpy(conf.ifname, "");
	eth_random_addr(conf.mac_addr.addr_bytes);
	conf.num_txq = 0;
	conf.num_rxq = 0;

	vport = init_vport_host(&conf);

	add_pipeline_v_to_sw(vport, sw);
	add_pipeline_sw_to_v(sw, vport);

	entry.in_port = vport->port.port_id;

	for (i = 0; i < num_pports; i++) {
		entry.dst_ip = rte_cpu_to_be_32(IPv4(10, 0, i, 1));
		entry.out_port = i;
		sw->ops->config(sw, &entry);

		entry.dst_ip = rte_cpu_to_be_32(IPv4(10, 0, i, 2));
		entry.out_port = i;
		sw->ops->config(sw, &entry);
	}
}

static void add_pipeline_v_to_p(struct vport *v, struct pport *p)
{
	char name[MODULE_NAME_LEN];

	struct module *v_inc;
	struct module *parse;
	struct module *flowsteer;
	struct module *tso;
	struct module *checksum;
	struct module *p_out;

	sprintf(name, "vport%hhu_inc", v->port.port_id);
	v_inc = init_module(name, &vport_inc_ops, 1, v);

	parse = init_module("parse", &parse_ops, 0, NULL);

	sprintf(name, "flowsteer_tx:%hhu", v->port.port_id);
	flowsteer = init_module(name, &flowsteer_tx_ops, 0, v);

	tso = init_module("tso", &tso_ops, 0, NULL);

	checksum = init_module("checksum_tx", &checksum_tx_ops, 0, NULL);

	sprintf(name, "pport%hhu_out", p->port.port_id);
	p_out = init_module(name, &pport_out_ops, 0, p);

	set_next_module(v_inc, parse);
	set_next_module(parse, flowsteer);
	set_next_module(flowsteer, tso);
	set_next_module(tso, checksum);
	set_next_module(checksum, p_out);
}

static void add_pipeline_p_to_v(struct pport *p, struct vport *v)
{
	char name[MODULE_NAME_LEN];

	struct module *p_inc;
	struct module *parse;
	struct module *checksum;
	struct module *lro;
	struct module *flowsteer;
	struct module *v_out;

	sprintf(name, "pport%hhu_inc", p->port.port_id);
	p_inc = init_module(name, &pport_inc_ops, 1, p);

	parse = init_module("parse", &parse_ops, 0, NULL);

	checksum = init_module("checksum_rx", &checksum_rx_ops, 0, NULL);

	lro = init_module("lro", &lro_ops, 0, NULL);

	sprintf(name, "flowsteer_rx:%hhu", v->port.port_id);
	flowsteer = init_module(name, &flowsteer_rx_ops, 0, v);

	sprintf(name, "vport%hhu_out", v->port.port_id);
	v_out = init_module(name, &vport_out_ops, 0, v);

	set_next_module(p_inc, parse);
	set_next_module(parse, checksum);
	set_next_module(checksum, lro);
	set_next_module(lro, flowsteer);
	set_next_module(flowsteer, v_out);
}

static void test_vm_bypass()
{
	struct pport *pport;

	struct vport *vport;
	struct vport_guest_config conf;

	/* physical ports */
	pport = init_pport(0, num_workers, num_workers);

	/* virtual ports */
	eth_random_addr(conf.mac_addr.addr_bytes);
	conf.num_txq = 4;
	conf.num_rxq = 4;
	conf.vmid = 0;
	vport = init_vport_guest(&conf);

	negotiate_vminfo();

	add_pipeline_p_to_v(pport, vport);
	add_pipeline_v_to_p(vport, pport);
}

static void test_many_vm()
{
	struct pport *pport;

	struct vport *vport[test_num_vports];
	struct vport_guest_config conf;

	struct vport *vport_host;
	struct vport_config conf_host;

	struct module *sw;

	int i;

	sw = init_module("sw_bridge", &sw_bridge_ops, 0, NULL);

	/* physical ports */
	for (i = 0; i < test_num_pports; i++) {
		pport = init_pport(i, num_workers, num_workers);

		add_pipeline_p_to_sw(pport, sw);
		add_pipeline_sw_to_p(sw, pport);
	}

	for (i = 0; i < test_num_vports; i++) {
		eth_random_addr(conf.mac_addr.addr_bytes);
		conf.num_txq = test_num_txq;
		conf.num_rxq = test_num_rxq;
		conf.vmid = i;
		vport[i] = init_vport_guest(&conf);
		set_vport_coalescing(vport[i], 0, 0);
	}

	if (test_num_vports > 0) {
		printf("Negotiation: waiting...\n");
		negotiate_vminfo();
		printf("Negotiation: done\n");

		for (i = 0; i < test_num_vports; i++) {
			add_pipeline_v_to_sw(vport[i], sw);
			add_pipeline_sw_to_v(sw, vport[i]);
		}
	}

	/* Host vport */
	strcpy(conf_host.ifname, "");
	eth_random_addr(conf_host.mac_addr.addr_bytes);
	conf_host.num_txq = 0;
	conf_host.num_rxq = 0;

	vport_host = init_vport_host(&conf_host);

	add_pipeline_v_to_sw(vport_host, sw);
	add_pipeline_sw_to_v(sw, vport_host);

#if 0
{
	struct miniflow_entry entry;

	/* XXX: This is for VXLAN test */
	assert(num_vm == 1);

	if (num_vm > 0) {
		entry.in_port = vport[0]->port.port_id;

		for (i = 0; i < num_pports; i++) {
			entry.dst_ip = rte_cpu_to_be_32(IPv4(10, 0, 100 + i, 1));
			entry.out_port = i;
			sw->ops->config(sw, &entry);

			entry.dst_ip = rte_cpu_to_be_32(IPv4(10, 0, 100 + i, 2));
			entry.out_port = i;
			sw->ops->config(sw, &entry);
		}
	}
}
#endif
}

static void test_ratelimiter()
{
	struct pport *pport;

	struct vport *vport;
	struct vport_config conf;

	struct module *sw;

	int i;

	struct miniflow_entry entry;

	sw = init_module("sw_bridge", &sw_bridge_ops, 0, NULL);

	/* physical ports (should be set earlier than vports) */
	for (i = 0; i < test_num_pports; i++) {
		pport = init_pport(i, num_workers, num_workers);

		add_pipeline_p_to_sw(pport, sw);
		add_pipeline_sw_to_p(sw, pport);
	}

	/* virtual ports */
	strcpy(conf.ifname, "");
	eth_random_addr(conf.mac_addr.addr_bytes);
	conf.num_txq = test_num_txq;
	conf.num_rxq = test_num_rxq;

	vport = init_vport_host(&conf);

	add_pipeline_v_to_sw(vport, sw);
	add_pipeline_sw_to_v(sw, vport);

	entry.in_port = vport->port.port_id;

	for (i = 0; i < test_num_pports; i++) {
		entry.dst_ip = rte_cpu_to_be_32(IPv4(10, 0, i, 1));
		entry.out_port = i;
		sw->ops->config(sw, &entry);

		entry.dst_ip = rte_cpu_to_be_32(IPv4(10, 0, i, 2));
		entry.out_port = i;
		sw->ops->config(sw, &entry);
	}
	struct module *m = find_module("ratelimiter");

	struct ratelimit_ctl ctl;
	ctl.cmd = SET_DEFAULT_RATE;
	ctl.rate = test_rate;
	
	(*m->ops->control)(m, &ctl);

#define CHAIN_TEST 0
#if CHAIN_TEST
	//add tenant tb
	ctl.cmd = ADD_TB;
	ctl.pport = 0;
	ctl.rate = rate * 3;
	ctl.tbid = 20002;
	ctl.burst = 1538 << 3;
	(*m->ops->control)(m, &ctl);

	//add vm tb
	ctl.cmd = ADD_TB;
	ctl.pport = 0;
	ctl.rate = rate * 2;
	ctl.tbid = 20000;
	ctl.burst = 1538 << 3;
	(*m->ops->control)(m, &ctl);

	ctl.cmd = CHAIN_TB;
	ctl.pport = 0;
	ctl.tbid = 20000;
	ctl.tbid2 = 20002;
	(*m->ops->control)(m, &ctl);

	//add vm tb
	ctl.cmd = ADD_TB;
	ctl.pport = 0;
	ctl.rate = rate * 2;
	ctl.tbid = 20001;
	ctl.burst = 1538 << 3;
	(*m->ops->control)(m, &ctl);

	ctl.cmd = CHAIN_TB;
	ctl.pport = 0;
	ctl.tbid = 20001;
	test_ctl.tbid2 = 20002;
	(*m->ops->control)(m, &ctl);
#endif
}


static void test_bypass()
{
	struct pport *pport;

	struct vport *vport;
	struct vport_config conf;

	/* physical ports */
	pport = init_pport(0, num_workers, num_workers);

	strcpy(conf.ifname, "");
	eth_random_addr(conf.mac_addr.addr_bytes);
	conf.num_txq = 0;
	conf.num_rxq = 0;

	/* virtual ports */
	vport = init_vport_host(&conf);
	set_vport_coalescing(vport, 100, 15);

	add_pipeline_p_to_v(pport, vport);
	add_pipeline_v_to_p(vport, pport);
}

static void test_native()
{
	int i;

	for (i = 0; i < test_num_pports; i++) {
		struct pport *pport;

		struct vport *vport;
		struct vport_config conf;

		char name[MODULE_NAME_LEN];

		struct module *p_inc;
		struct module *v_out;

		struct module *v_inc;
		struct module *p_out;

		/* physical ports */
		pport = init_pport(i, num_workers, num_workers);

		/* virtual ports (native) */
		sprintf(conf.ifname, "raw%d", i);
		eth_random_addr(conf.mac_addr.addr_bytes);
		conf.num_txq = num_workers;
		conf.num_rxq = num_workers;
		vport = init_vport_host_native(&conf);

		/* pport -> vport */
		sprintf(name, "pport%d_inc", i);
		p_inc = init_module(name, &pport_inc_ops, 1, pport);

		sprintf(name, "vport%d_out", i);
		v_out = init_module(name, &vport_out_ops, 0, vport);

		set_next_module(p_inc, v_out);

		/* vport -> pport */
		sprintf(name, "vport%d_inc", i);
		v_inc = init_module(name, &vport_inc_ops, 1, vport);

		sprintf(name, "pport%d_out", i);
		p_out = init_module(name, &pport_out_ops, 0, pport);

		set_next_module(v_inc, p_out);
	}
}

static void test_p2p()
{
	int i;

	for (i = 0; i < test_num_pports; i++) {
		struct pport *pport;

		struct module *p_inc;
		struct module *p_out;

		char name[MODULE_NAME_LEN];

		pport = init_pport(i, num_workers, num_workers);

		sprintf(name, "pport%d_inc", i);
		p_inc = init_module(name, &pport_inc_ops, 1, pport);

		sprintf(name, "pport%d_out", i);
		p_out = init_module(name, &pport_out_ops, 0, pport);

		set_next_module(p_inc, p_out);
	}
}

static void test_v2v()
{
	const int cpu_offset = 1;

	struct vport *vport[test_num_vports];
	struct module *v_inc[test_num_vports];
	struct module *v_out[test_num_vports];

	struct module *sink;

	int i;

	for (i = 0; i < test_num_vports; i++) {
		struct vport_config conf;

		char name[MODULE_NAME_LEN];

		struct sn_ioc_queue_mapping map;
		int cpu;


		sprintf(conf.ifname, "sn%d", i);
		eth_random_addr(conf.mac_addr.addr_bytes);
		conf.num_txq = 1;
		conf.num_rxq = 1;

		vport[i] = init_vport_host(&conf);

		map.rxq_to_cpu[0] = cpu_offset + i;
		for (cpu = 0; cpu < SN_MAX_CPU; cpu++)
			map.cpu_to_txq[cpu] = 0;

		vport_apply_queue_mapping(vport[i], &map);

		set_vport_coalescing(vport[i], 10, 10);

		sprintf(name, "vport%d_inc", i);
		v_inc[i] = init_module(name, &vport_inc_ops, 1, vport[i]);

		sprintf(name, "vport%d_out", i);
		v_out[i] = init_module(name, &vport_out_ops, 0, vport[i]);
	}
		
	sink = init_module("sink", &sink_ops, 0, NULL);

	for (i = 0; i < test_num_vports; i++)
		set_next_module(v_inc[i], v_out[i]);
		//set_next_module(v_inc[i], v_out[(i + 1) % test_num_vports]);
		//set_next_module(v_inc[i], sink);
}


static void test_n2n()
{
	struct vport_config conf;
	struct vport *vport[2];

	struct module *v_inc[2];
	struct module *v_out[2];

	char name[MODULE_NAME_LEN];

	conf.num_txq = num_workers;
	conf.num_rxq = num_workers;

	/* vport 0 */
	eth_random_addr(conf.mac_addr.addr_bytes);
	sprintf(conf.ifname, "raw0");
	vport[0] = init_vport_host_native(&conf);

	sprintf(name, "vport0_inc");
	v_inc[0] = init_module(name, &vport_inc_ops, 1, vport[0]);

	sprintf(name, "vport0_out");
	v_out[0] = init_module(name, &vport_out_ops, 0, vport[0]);

	/* vport 1 */
	eth_random_addr(conf.mac_addr.addr_bytes);
	sprintf(conf.ifname, "raw1");
	vport[1] = init_vport_host_native(&conf);

	sprintf(name, "vport1_inc");
	v_inc[1] = init_module(name, &vport_inc_ops, 1, vport[1]);

	sprintf(name, "vport1_out");
	v_out[1] = init_module(name, &vport_out_ops, 0, vport[1]);

	/* connect */
	set_next_module(v_inc[0], v_out[1]);
	set_next_module(v_inc[1], v_out[0]);
}

static void test_s2h()
{
	const int cpu_offset = 1;

	struct vport_config conf;
	struct vport *vport;

	struct module *source;
	struct module *v_out;

	int i;

	for (i = 0; i < test_num_vports; i++) {
		char buf[128];
		int ret;

		struct sn_ioc_queue_mapping map;
		int cpu;

		sprintf(buf, "source%d", i);
		source = init_module(buf, &source_ops, 1, NULL);

		sprintf(conf.ifname, "dummy%d", i);
		eth_random_addr(conf.mac_addr.addr_bytes);
		conf.num_txq = 1;
		conf.num_rxq = 1;
		vport = init_vport_host(&conf);

		map.rxq_to_cpu[0] = cpu_offset + i;
		for (cpu = 0; cpu < SN_MAX_CPU; cpu++)
			map.cpu_to_txq[cpu] = 0;

		vport_apply_queue_mapping(vport, &map);

		sprintf(buf, "vport%d_out", i);
		v_out = init_module(buf, &vport_out_ops, 0, vport);

		set_next_module(source, v_out);

		sprintf(buf, "sudo ifconfig %s up", conf.ifname);
		ret = system(buf);
	}

	/* wait for the vport to be fully up */
	sleep(2);
}

static void test_s2s()
{
	struct module *source;
	struct module *sink;

	source = init_module("source", &source_ops, 1, NULL);
	sink = init_module("sink", &sink_ops, 0, NULL);
	set_next_module(source, sink);
}

static void test_p2s()
{
	int i;

	for (i = 0; i < test_num_pports; i++) {
		struct pport *pport;

		struct module *p_inc;
		struct module *sink;

		char name[MODULE_NAME_LEN];

		pport = init_pport(i, num_workers, num_workers);

		sprintf(name, "pport%d_inc", i);
		p_inc = init_module(name, &pport_inc_ops, 1, pport);

		sprintf(name, "sink%d", i);
		sink = init_module(name, &sink_ops, 0, NULL);

		set_next_module(p_inc, sink);
	}
}

static void test_s2p()
{
	int i;

	for (i = 0; i < test_num_pports; i++) {
		struct pport *pport;

		struct module *source;
		struct module *p_out;

		char name[MODULE_NAME_LEN];

		sprintf(name, "source%d", i);
		source = init_module(name, &source_ops, 1, NULL);

		pport = init_pport(i, num_workers, num_workers);

		sprintf(name, "pport%d_out", i);
		p_out = init_module(name, &pport_out_ops, 0, pport);

		set_next_module(source, p_out);
	}
}

static void add_pipeline_p_lb_v(struct pport *p,
				struct module *lb,
				struct vport *v)
{
	char name[MODULE_NAME_LEN];

	struct module *p_inc;
	struct module *parse;
	struct module *v_out;

	sprintf(name, "pport%hhu_inc", p->port.port_id);
	p_inc = init_module(name, &pport_inc_ops, 1, p);

	parse = init_module("parse", &parse_ops, 0, NULL);

	sprintf(name, "vport%hhu_out", v->port.port_id);
	v_out = init_module(name, &vport_out_ops, 0, v);

	set_next_module(p_inc, parse);
	set_next_module(parse, lb);
	add_next_module(lb, v_out, &v->port);
}

static void add_pipeline_v_to_p_raw(struct vport *v, struct pport *p)
{
	char name[MODULE_NAME_LEN];

	struct module *v_inc;
	struct module *p_out;

	sprintf(name, "vport%hhu_inc", v->port.port_id);
	v_inc = init_module(name, &vport_inc_ops, 1, v);

	sprintf(name, "pport%hhu_out", p->port.port_id);
	p_out = init_module(name, &pport_out_ops, 0, p);

	set_next_module(v_inc, p_out);
}

/* snort test */
static void test_lb()
{
	struct pport *pport_inc;
	struct pport *pport_out;
	struct module *lb;

	const int num_vport_pairs = 4;
	int i;

	/* physical port */
	pport_inc = init_pport(0, num_workers, num_workers);
	pport_out = init_pport(1, num_workers, num_workers);

	lb = init_module("lb", &lb_ops, 0, NULL);

	/* virtual ports */
	for (i = 0; i < num_vport_pairs; i++) {
		struct vport *vport_rx;
		struct vport *vport_tx;
		struct vport_config conf;

		struct sn_ioc_queue_mapping map;
		int cpu;

		/* RX-only interface */
		strcpy(conf.ifname, "");
		eth_random_addr(conf.mac_addr.addr_bytes);
		conf.num_txq = 1;
		conf.num_rxq = 1;
		vport_rx = init_vport_host(&conf);

		map.rxq_to_cpu[0] = i + 1;	/* assumes cpu 0 is softnic */
		for (cpu = 0; cpu < SN_MAX_CPU; cpu++)
			map.cpu_to_txq[cpu] = 0;

		vport_apply_queue_mapping(vport_rx, &map);

		add_pipeline_p_lb_v(pport_inc, lb, vport_rx);

		/* TX-only interface */
		strcpy(conf.ifname, "");
		eth_random_addr(conf.mac_addr.addr_bytes);
		conf.num_txq = 1;
		conf.num_rxq = 1;
		vport_tx = init_vport_host(&conf);

		add_pipeline_v_to_p_raw(vport_tx, pport_out);
	}

	for (i = 0; i < num_vport_pairs * 2; i++) {
		char cmdbuf[512];
		int ret;

		sprintf(cmdbuf, "sudo ifconfig sn%d up", i);
		ret = system(cmdbuf);
		assert(WEXITSTATUS(ret) == 0);
	}
}

static void test_noop()
{
	struct module *noop;

	noop = init_module("noop", &noop_ops, 1, NULL);
}

static void test_nil()
{
	/* do nothing */
}

#endif

static int core_to_socket_id(int cpu)
{
	char line[256];
	char *tmp;
	FILE *fp;

	int ret;
	int i;

	fp = popen("cat /proc/cpuinfo | grep \"physical id\"", "r");
	assert(fp);

	for (i = 0; i < cpu; i++) {
		tmp = fgets(line, sizeof(line), fp);
		assert(tmp == line);
	}

	tmp = fgets(line, sizeof(line), fp);
	assert(tmp == line);

	sscanf(line, "physical id\t: %d", &ret);

	fclose(fp);

	return ret;
}

static struct {
	int wid_to_core[MAX_WORKERS];
	uint16_t port;
} cmdline_opts;

static int daemonize = 1;

static void parse_core_list()
{
	char *ptr;

	ptr = strtok(optarg, ",");
	while (ptr != NULL) {
		if (num_workers >= MAX_WORKERS) {
			fprintf(stderr, "Cannot have more than %d workers\n",
					MAX_WORKERS);
			exit(EXIT_FAILURE);
		}

		cmdline_opts.wid_to_core[num_workers] = atoi(ptr);
		num_workers++;
		ptr = strtok(NULL, ",");
	}
}

static void print_usage(char *exec_name)
{
	fprintf(stderr, "Usage: %s [-t] [-c <core list>] [-p <port>]\n",
			exec_name);
	fprintf(stderr, "\n");

	fprintf(stderr, "  %-16s Dump the size of internal data structures\n",
			"-t");
	fprintf(stderr, "  %-16s Core ID for each worker (e.g., -c 0, 8)\n",
			"-c <core list>");
	fprintf(stderr, "  %-16s Specifies the TCP port on which SoftNIC listens"
			     "for controller connections\n",
			"-p <port>");
	fprintf(stderr, "  %-16s Do not daemonize BESS",
			"-w");

	exit(2);
}

/* NOTE: At this point DPDK has not been initilaized, 
 *       so it cannot invoke rte_* functions yet. */
static void init_config(int argc, char **argv)
{
	char c;

	num_workers = 0;

	while ((c = getopt(argc, argv, ":tc:p:w")) != -1) {
		switch (c) {
		case 't':
			dump_types();
			exit(EXIT_SUCCESS);
			break;

		case 'c':
			parse_core_list();
			break;

		case 'p':
			sscanf(optarg, "%hu", &cmdline_opts.port);
			break;
		case 'w':
			daemonize = 0;
			break;

		case ':':
			fprintf(stderr, "argument is required for -%c\n", 
					optopt);
			print_usage(argv[0]);
			break;

		case '?':
			fprintf(stderr, "Invalid option -%c\n", optopt);
			print_usage(argv[0]);
			break;

		default:
			assert(0);
		}
	}

	if (num_workers == 0) {
		/* By default, launch one worker on core 0 */
		cmdline_opts.wid_to_core[0] = 0;
		num_workers = 1;
	}
}

int main(int argc, char **argv)
{
	pid_t pid, sid;
	init_config(argc, argv);

	if (daemonize) {
		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "Could not fork damon\n");
			goto fail;
		}
		if (pid > 0) {
			exit(EXIT_SUCCESS);
		}
		// Reparent
		sid = setsid();
		if (sid < 0) {
			goto fail;
		}

		close(STDIN_FILENO);
		close(STDERR_FILENO);
		close(STDOUT_FILENO);
		setup_syslog();
	}
	init_dpdk(argv[0]);
	init_mempool();
	init_drivers();

	for (int i = 0; i < num_workers; i++)
		launch_worker(i, cmdline_opts.wid_to_core[i]);

	run_master(cmdline_opts.port);

	if (daemonize)
		end_syslog();
fail:
	/* never executed */
	rte_eal_mp_wait_lcore();
	close_mempool();

	return 0;
}
