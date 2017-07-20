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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <rte_cycles.h>
#include <rte_config.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_byteorder.h>

#include "../kmod/sn_common.h"

#include "sn.h"

char appinstance_name[APPNAMESIZ];

struct rte_mempool *mempool;

/*const int ATTRSZ = VALUESZ;*/

struct rte_mbuf rte_mbuf_template;

static void init_template(void)
{
	struct rte_mbuf *mbuf;

	mbuf = rte_pktmbuf_alloc(mempool);
	rte_mbuf_template = *mbuf;
	rte_pktmbuf_free(mbuf);
}


static void load_mempool(void)
{
	/* Try pframe pool on node 0 first */
	const int BEGIN = 16384;
	const int END = 524288;
	char name[256];
	for (int i = BEGIN; i <= END; i *= 2) {
		sprintf(name, "pframe0_%dk", (i + 1) / 1024);
		mempool = rte_mempool_lookup(name);
		if (mempool)
			return;
	}

	for (int i = BEGIN; i <= END; i *= 2) {
		sprintf(name, "pframe1_%dk", (i + 1) / 1024);
		mempool = rte_mempool_lookup(name);
		if (mempool)
			return;
	}

	assert(0);
}

/* FIXME */
/*int push_metadata_tag(struct snbuf *pkt,*/
        /*const char *key, const char *value)*/
/*{*/
    /*int ret = -1;*/

    /*struct metadata_hdr *meta;*/
    /*struct ether_hdr *ether, *orig;*/

    /*orig = (struct ether_hdr *)pkt;*/

    /*// only one tag allowed per packet for now*/
    /*if (orig->ether_type == ETHER_TYPE_ESMETA)*/
        /*goto exit;*/

    /*ether = (struct ether_hdr *)rte_pktmbuf_prepend(&pkt->mbuf,*/
            /*sizeof(struct ether_hdr) + sizeof(struct metadata_hdr));*/

    /*if (!ether) {*/
        /*goto exit;*/
        /*printf("no headroom!\n");*/
    /*}*/

    /*meta = (struct metadata_hdr *)(ether + 1);*/
    /*meta->key_64 = 0;*/
    /*meta->val_64 = 0;*/

    /*// Why do I even add an ethernet header? TODO check if bess will be upset*/
    /*// if this isn't present*/
    /*ether->ether_type = rte_cpu_to_be_16(ETHER_TYPE_ESMETA);*/
    /*ether_addr_copy(&orig->d_addr, &ether->d_addr);*/
    /*ether_addr_copy(&orig->s_addr, &ether->s_addr);*/

    /*// Copy the metadata*/
    /*memset(meta, 0, sizeof(struct metadata_hdr));*/
    /*strncpy(meta->key, key, ATTRSZ);*/
    /*strncpy(meta->value, value, ATTRSZ);*/

    /*ret = 0;*/

/*exit:*/
    /*return ret;*/
/*}*/

void sn_init_thread (uint32_t lcore) {
	eal_thread_init_master(lcore);
}

void init_bess(uint32_t lcore, char *name)
{
	int rte_argc;
	char *rte_argv[16];

	char opt_core_bitmap[256];

	int ret;

	uint64_t cpumask = (1ull << lcore);

	sprintf(opt_core_bitmap, "0x%lx", cpumask);

	rte_argc = 7;
	rte_argv[0] = "";
	rte_argv[1] = "-c";
	rte_argv[2] = opt_core_bitmap;
	rte_argv[3] = "-n";
	rte_argv[4] = "4";	/* number of mem channels (Sandy/Ivy Bridge) */
	rte_argv[5] = "--proc-type";
	rte_argv[6] = "secondary";
	rte_argv[7] = NULL;

	/* reset getopt() */
	optind = 0;

	ret = rte_eal_init(rte_argc, rte_argv);
	assert(ret >= 0);

	load_mempool();

	assert(strlen(name) < APPNAMESIZ);
	strcpy(appinstance_name, name);

	init_template();
}

/* return NULL if the device is not found */
struct sn_port *init_port(const char *ifname)
{
	struct sn_port *port;
	struct vport_bar *bar;
	int i;

	FILE* fd;
	char port_file[PORT_FNAME_LEN];

	snprintf(port_file, PORT_FNAME_LEN, "%s/%s/%s",
			P_tmpdir, VPORT_DIR_PREFIX, ifname);
	fd = fopen(port_file, "r");
	if (!fd) {
		return NULL;
	}
	i = fread(&bar, 8, 1, fd);
	fclose(fd);
	if (i != 1) {
		return NULL;
	}

	if (!bar)
		return NULL;

	port = malloc(sizeof(struct sn_port));
	port->bar = bar;

	port->num_txq = bar->num_inc_q;
	port->num_rxq = bar->num_out_q;

	for (i = 0; i < port->num_rxq; i++) {
		port->rx_regs[i] = bar->out_regs[i];
		port->rx_qs[i] = bar->out_qs[i];
	}

	for (i = 0; i < port->num_txq; i++) {
		port->tx_regs[i] = bar->inc_regs[i];
		port->tx_qs[i] = bar->inc_qs[i];
	}

	for (i = 0; i < port->num_rxq; i++) {
		char fifoname[256];

		sprintf(fifoname, "%s/%s/%s.rx%d",
				P_tmpdir, VPORT_DIR_PREFIX, ifname, i);

		port->fd[i] = open(fifoname, O_RDONLY);
		assert(port->fd[i] > 0);
	}

	return port;
}

void sn_enable_interrupt(struct vport_out_regs *rx_regs)
{
	__sn_enable_interrupt(rx_regs);
}

void sn_disable_interrupt(struct vport_out_regs *rx_regs)
{
	__sn_disable_interrupt(rx_regs);
}

void close_port(struct sn_port *port)
{
	free(port);
}

int sn_receive_pkts(struct sn_port *port,
		 int rxq, struct snbuf **pkts, int cnt)
{
	return __receive_pkts(port, rxq, pkts, cnt);
}
int sn_send_pkts(struct sn_port *port,
	       int txq, struct snbuf **pkts, int cnt)
{
	return __send_pkts(port, txq, pkts, cnt);
}

struct snbuf *sn_snb_alloc(void)
{
	return __sn_snb_alloc();
}

void sn_snb_free(struct snbuf *pkt)
{
	__sn_snb_free(pkt);
}

void sn_snb_alloc_bulk(snb_array_t pkts, int cnt)
{
	__sn_snb_alloc_bulk(pkts, cnt);
}

void sn_snb_free_bulk(snb_array_t pkts, int cnt)
{
	__sn_snb_free_bulk(pkts, cnt);
}

void sn_snb_free_bulk_range(snb_array_t pkts, int start, int cnt) {
	__sn_snb_free_bulk(&pkts[start], cnt);
}

uint32_t sn_get_lcore_id() {
	return rte_lcore_id();
}

/* Fast memcpy for certain architectures using rep movsb */
static inline void *__movsb(void *dest, const void *src, size_t n) {
        asm __volatile__ ("rep movsb" /* Issue movsb command */
                : "=D" (dest), /* Place dest in RDI*/
                  "=S" (src),  /* Place src in RSI*/
                  "=c" (n)     /* Place n in rcx*/
                : "0" (dest),  /* Inputs */
                  "1" (src),
                  "2" (n)
                : "memory");  /* Expect changes in memory */
        return dest;
}

void sn_snb_copy_batch(snb_array_t src, snb_array_t dest, int cnt) {
	// First allocate
	__sn_snb_alloc_bulk(dest, cnt);
	for (int i=0; i<cnt; i++) {
		dest[i]->mbuf.data_len = dest[i]->mbuf.pkt_len = src[i]->mbuf.data_len;
		rte_memcpy(snb_head_data(dest[i]), snb_head_data(src[i]), src[i]->mbuf.data_len);
	}
}

uint16_t sn_num_txq(struct sn_port* vport) {
	return vport->num_txq;
}

uint16_t sn_num_rxq(struct sn_port* vport) {
	return vport->num_rxq;
}

uint64_t sn_rdtsc() {
	return rte_rdtsc();
}

void sn_wait(long cycles) {
	uint64_t start, end;
	start = rte_rdtsc();
	end = start;
	while (end - start < cycles)
		end = rte_rdtsc();
}
