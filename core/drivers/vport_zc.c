#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <rte_malloc.h>
#include <rte_config.h>

#include "../driver.h"
#include "../port.h"
#include "../snbuf.h"

#define SLOTS_PER_LLRING	1024

/* This watermark is to detect congestion and cache bouncing due to
 * head-eating-tail (needs at least 8 slots less then the total ring slots).
 * Not sure how to tune this... */
#define SLOTS_WATERMARK		((SLOTS_PER_LLRING >> 3) * 7)	/* 87.5% */

/* Disable (0) single producer/consumer mode for now.
 * This is slower, but just to be on the safe side. :) */
#define SINGLE_P		0
#define SINGLE_C		0

#define VPORT_DIR_PREFIX "sn_vports"

struct vport_inc_regs {
	uint64_t dropped;
} __cacheline_aligned;

struct vport_out_regs {
	uint32_t irq_enabled;
} __cacheline_aligned;

/* This is equivalent to the old bar */
struct vport_bar {
	char name[PORT_NAME_LEN];

	/* The term RX/TX could be very confusing for a virtual switch.
	 * Instead, we use the "incoming/outgoing" convention:
	 * - incoming: outside -> SoftNIC
	 * - outgoing: SoftNIC -> outside */
	int num_inc_q;
	int num_out_q;

	struct vport_inc_regs* inc_regs[MAX_QUEUES_PER_DIR];
	struct llring* inc_qs[MAX_QUEUES_PER_DIR];

	struct vport_out_regs* out_regs[MAX_QUEUES_PER_DIR];
	struct llring* out_qs[MAX_QUEUES_PER_DIR];
};

struct vport_priv {
	struct vport_bar* bar;

	struct vport_inc_regs* inc_regs[MAX_QUEUES_PER_DIR];
	struct llring* inc_qs[MAX_QUEUES_PER_DIR];

	struct vport_out_regs* out_regs[MAX_QUEUES_PER_DIR];
	struct llring* out_qs[MAX_QUEUES_PER_DIR];

	int out_irq_fd[MAX_QUEUES_PER_DIR];
};

static struct snobj *vport_init_port(struct port *p, struct snobj *arg)
{
	struct vport_priv *priv = get_port_priv(p);
	struct vport_bar *bar = NULL;

	int num_inc_q = p->num_queues[PACKET_DIR_INC];
	int num_out_q = p->num_queues[PACKET_DIR_OUT];

	int bytes_per_llring;
	int total_bytes;
	uint8_t *ptr;
	int i;
	char port_dir[PORT_NAME_LEN + 256];
	char file_name[PORT_NAME_LEN + 256];
	struct stat sb;
	FILE* fp;
	size_t bar_address;

	bytes_per_llring = llring_bytes_with_slots(SLOTS_PER_LLRING);
	total_bytes =	sizeof(struct vport_bar) +
			(bytes_per_llring * (num_inc_q + num_out_q)) +
			(sizeof(struct vport_inc_regs) * (num_inc_q)) +
			(sizeof(struct vport_out_regs) * (num_out_q));

	bar = rte_zmalloc(NULL, total_bytes, 0);
	bar_address = (size_t)bar;
	assert(bar != NULL);
	priv->bar = bar;

	strncpy(bar->name, p->name, PORT_NAME_LEN);
	bar->num_inc_q = num_inc_q;
	bar->num_out_q = num_out_q;

	ptr = (uint8_t*)(bar + 1);

	/* Set up inc llrings */
	for (i = 0; i < num_inc_q; i++) {
		priv->inc_regs[i] = bar->inc_regs[i] =
			(struct vport_inc_regs*)ptr;
		ptr += sizeof(struct vport_inc_regs);

		llring_init((struct llring *)ptr, SLOTS_PER_LLRING,
				SINGLE_P, SINGLE_C);
		llring_set_water_mark((struct llring *)ptr, SLOTS_WATERMARK);
		bar->inc_qs[i] = (struct llring *)ptr;
		priv->inc_qs[i] = bar->inc_qs[i];
		ptr += bytes_per_llring;
	}

	/* Set up out llrings */
	for (i = 0; i < num_out_q; i++) {
		priv->out_regs[i] = bar->out_regs[i] =
			(struct vport_out_regs*)ptr;
		ptr += sizeof(struct vport_out_regs);

		llring_init((struct llring *)ptr, SLOTS_PER_LLRING,
				SINGLE_P, SINGLE_C);
		llring_set_water_mark((struct llring *)ptr, SLOTS_WATERMARK);
		bar->out_qs[i] = (struct llring *)ptr;
		priv->out_qs[i] = bar->out_qs[i];
		ptr += bytes_per_llring;
	}

	snprintf(port_dir, PORT_NAME_LEN + 256, "%s/%s",
			P_tmpdir, VPORT_DIR_PREFIX);

	if (stat(port_dir, &sb) == 0) {
		assert((sb.st_mode & S_IFMT) == S_IFDIR);
	} else {
		printf("Creating directory %s\n", port_dir);
		assert(errno == ENOENT);
		mkdir(port_dir, S_IRWXU | S_IRWXG | S_IRWXO);
	}

	for (i = 0; i < num_out_q; i++) {
		snprintf(file_name, PORT_NAME_LEN + 256, "%s/%s/%s.rx%d",
				P_tmpdir, VPORT_DIR_PREFIX, p->name, i);

		mkfifo(file_name, 0666);

		priv->out_irq_fd[i] = open(file_name, O_RDWR);
	}

	snprintf(file_name, PORT_NAME_LEN + 256, "%s/%s/%s",
			P_tmpdir, VPORT_DIR_PREFIX, p->name);
	printf("Writing port information to %s\n", file_name);
	fp = fopen(file_name, "w");
	fwrite(&bar_address, 8, 1, fp);
	fclose(fp);

	return NULL;
}

static void vport_deinit_port(struct port *p)
{
	struct vport_priv *priv = get_port_priv(p);
	char file_name[PORT_NAME_LEN + 256];

	int num_out_q = p->num_queues[PACKET_DIR_OUT];

	for (int i = 0; i <num_out_q; i++) {
		snprintf(file_name, PORT_NAME_LEN + 256, "%s/%s/%s.rx%d",
				P_tmpdir, VPORT_DIR_PREFIX, p->name, i);
		
		unlink(file_name);
		close(priv->out_irq_fd[i]);
	}

	snprintf(file_name, PORT_NAME_LEN + 256, "%s/%s/%s",
			P_tmpdir, VPORT_DIR_PREFIX, p->name);
	unlink(file_name);

	rte_free(priv->bar);
}

static int
vport_send_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
	struct vport_priv *priv = get_port_priv(p);
	struct llring* q = priv->out_qs[qid];
	int ret;
	
	ret = llring_enqueue_bulk(q, (void**)pkts, cnt);
	if (ret == -LLRING_ERR_NOBUF)
		return 0;

	if (__sync_bool_compare_and_swap(&priv->out_regs[qid]->irq_enabled,
				1, 0))
	{
		int ret;
		ret = write(priv->out_irq_fd[qid], (void *)&(char){'F'}, 1);
	}

	return cnt;
}

static int
vport_recv_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
	struct vport_priv *priv = get_port_priv(p);
	struct llring* q = priv->inc_qs[qid];
	int ret;
	
	ret = llring_dequeue_burst(q, (void **)pkts, cnt);
	return ret;
}

static const struct driver vport = {
	.name 		= "ZeroCopyVPort",
	.def_port_name	= "zcvport",
	.priv_size	= sizeof(struct vport_priv),
	.init_port 	= vport_init_port,
	.recv_pkts 	= vport_recv_pkts,
	.send_pkts 	= vport_send_pkts,
};

ADD_DRIVER(vport)
