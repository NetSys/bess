/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2014 Sangjin Han All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/* Dual BSD/GPL */

#ifndef _SN_COMMON_H_
#define _SN_COMMON_H_

#include <linux/if_ether.h>

#ifdef __KERNEL__

#include <linux/if.h>
#include <linux/module.h>

#else

#define __user
#define IFNAMSIZ 16

#include <stdint.h>

typedef uint64_t phys_addr_t;

#endif

#include "llring.h"

#define SN_MAX_CPU 64

#define SN_MAX_TXQ 32
#define SN_MAX_RXQ 32

#define SN_IOC_CREATE_HOSTNIC 0x8501
#define SN_IOC_RELEASE_HOSTNIC 0x8502
#define SN_IOC_KICK_RX 0x8503
#define SN_IOC_SET_QUEUE_MAPPING 0x8504

struct sn_ioc_queue_mapping {
	int cpu_to_txq[SN_MAX_CPU];
	int rxq_to_cpu[SN_MAX_RXQ];
};

struct tx_queue_opts {
	/* If set, the driver will push tags for all xmitted packets.
	 * Both are in host order. */
	uint16_t tci;
	uint16_t outer_tci;
};

struct rx_queue_opts {
	uint8_t loopback;
};

struct sn_conf_space {
	uint64_t bar_size;

	int netns_fd;      /* < 0 if unset */
	int container_pid; /* = 0 if unset */

	char ifname[IFNAMSIZ]; /* in/out argument */
	uint8_t mac_addr[ETH_ALEN];

	uint16_t num_txq;
	uint16_t num_rxq;

	/* currently not used */
	uint8_t link_on;
	uint8_t promisc_on;

	struct tx_queue_opts txq_opts;
	struct rx_queue_opts rxq_opts;
} __attribute__((__aligned__(64)));

struct sn_rxq_registers {
	/* Set by the kernel driver, to suppress bogus interrupts */
	volatile uint32_t irq_disabled;

	/* Separate this from the shared cache line */
	uint64_t dropped __attribute__((__aligned__(64)));
} __attribute__((__aligned__(64)));

/* Do not attempt to calculate checksum for this TX packet */
#define SN_TX_CSUM_DONT 65535

#define SN_TX_FRAG_MAX_NUM 18 /*(MAX_SKB_FRAGS + 1)*/

/* Driver -> BESS metadata for TX packets */
struct sn_tx_metadata {
	/* Both are relative offsets from the beginning of the packet.
	 * The sender should set csum_start to CSUM_DONT
	 * if no checksumming is wanted (csum_dest is undefined).*/
	uint16_t csum_start;
	uint16_t csum_dest;
};

struct sn_tx_desc {
	uint16_t total_len;

	struct sn_tx_metadata meta;
};

#define SN_RX_CSUM_UNEXAMINED 0
#define SN_RX_CSUM_UNKNOWN_P 1 /* unknown protocol */
#define SN_RX_CSUM_INCORRECT 2
#define SN_RX_CSUM_CORRECT 3
#define SN_RX_CSUM_CORRECT_ENCAP 4

struct sn_rx_metadata {
	/* Maximum TCP "payload" size among coalesced packets.
	 * 0 for non-coalesed packets */
	uint16_t gso_mss;

	uint8_t csum_state; /* SN_RX_CSUM_* */
};

/* BESS -> Driver descriptor for RX packets */
struct sn_rx_desc {
	uint32_t total_len;

	/* Only the following three fields are valid for non-head segments */
	uint16_t seg_len;
	phys_addr_t seg; /* where's the actual data? */

	/* The physical address of next snbuf
	 * (forms a NULL-terminating linked list) */
	phys_addr_t next;

	struct sn_rx_metadata meta;
};

/* BAR layout
 *
 * struct sn_conf_space (set by BESS and currently read-only)
 * TX queue 0 llring (drv -> sn)
 * TX queue 0 llring (sn -> drv)
 * TX queue 1 llring (drv -> sn)
 * TX queue 1 llring (sn -> drv)
 * ...
 * RX queue 0 registers
 * RX queue 0 llring (drv -> sn)
 * RX queue 0 llring (sn -> drv)
 * RX queue 1 registers
 * RX queue 1 llring (drv -> sn)
 * RX queue 1 llring (sn -> drv)
 * ...
 */

/* TX:
 * BESS feeds buffers to the driver via the sn_to_drv llring, in this order:
 *   1. Cookie  2. Address of the buffer (physical)
 * The buffer must be at least as big as sizeof(sn_tx_metadata) + SNBUF_DATA.
 *
 * Then the driver will copy (metedata + packet data) _into_ those buffers
 * as packets are transmitted, and writeback the cookie via the drv_to_sn.
 *   1. Cookie
 *
 *
 * RX:
 * BESS feeds received packet buffers to the driver via the sn_to_drv llring,
 * in this order:
 *   1. Cookie  2. Address of the buffer (physical)
 * The buffer must be at least as big as sizeof(sn_tx_metadata) + packet size
 *
 * Then the driver will copy (metadata + packet data) _from_ those buffers,
 * and writeback the cookie via the drv_to_sn.
 *   1. Cookie
 */

#endif
