#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <libgen.h>

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <rte_malloc.h>

#include "../port.h"
#include "../snbuf.h"

/* TODO: Unify vport and vport_native */

#define SLOTS_PER_LLRING	1024

/* This watermark is to detect congestion and cache bouncing due to
 * head-eating-tail (needs at least 8 slots less then the total ring slots).
 * Not sure how to tune this... */
#define SLOTS_WATERMARK		((SLOTS_PER_LLRING >> 3) * 7)	/* 87.5% */

/* Disable (0) single producer/consumer mode for now.
 * This is slower, but just to be on the safe side. :) */
#define SINGLE_P		0
#define SINGLE_C		0

struct queue {
	union {
		struct sn_rxq_registers *rx_regs;
	};

	struct llring *drv_to_sn;
	struct llring *sn_to_drv;
};

struct vport_priv {
	int fd;

	void *bar;

	struct queue inc_qs[MAX_QUEUES_PER_DIR];
	struct queue out_qs[MAX_QUEUES_PER_DIR];

	struct sn_ioc_queue_mapping map;
};

static int next_cpu; 

static inline int find_next_nonworker_cpu(int cpu) 
{
	do {
		cpu = (cpu + 1) % sysconf(_SC_NPROCESSORS_ONLN);
	} while (is_worker_core(cpu));
	return cpu;
}

static void refill_tx_bufs(struct llring *r, int cnt)
{
	struct snbuf *snb;
	void *objs[cnt * 2];

	int ret;
	int i;
	
	int deficit = r->common.watermark / 2 - llring_count(r) / 2;

	if (0 <= deficit && deficit < cnt)
		cnt = deficit;

	for (i = 0; i < cnt; i++) {
		snb = snb_alloc(SNBUF_LFRAME);
		if (snb == NULL) {
			cnt = i;
			break;
		}

		objs[i * 2    ] = (void *) snb;
		objs[i * 2 + 1] = (void *) snb_dma_addr(snb);
	}
	
	ret = llring_enqueue_bulk(r, objs, cnt * 2);
	assert(ret == 0 || ret == -LLRING_ERR_QUOT);
}

static void drain_drv_to_sn_q(struct llring *q)
{
	/* drv_to_sn queues just contain allocated buffers, so just dequeue bufs
	 * and free */
	const int MAX_BURST = 64;
	void *objs[MAX_BURST];
	int ret;
	while ((ret = llring_dequeue_burst(q, objs, MAX_BURST)) > 0) {
		snb_free_bulk((snb_array_t) objs, ret);
	}
	
}

static void drain_sn_to_drv_q(struct llring *q)
{
	/* sn_to_drv queues have alternate cells containing allocated buffers
	 * and their physical address */
	const int MAX_BURST = 64;
	void *objs[MAX_BURST * 2];
	void *clean_objs[MAX_BURST];
	int ret;
	while ((ret = llring_dequeue_burst(q, objs, 2 * MAX_BURST)) > 0) {
		int j = 0;
		for (int i = 0; i < ret; i += 2) {
			clean_objs[j++] = objs[i];
		}
		snb_free_bulk((snb_array_t)clean_objs, j);
	}
}

/* Free an allocated bar, freeing resources in the queues */
static void free_bar(struct vport_priv *priv)
{
	int i;
	struct sn_conf_space *conf = priv->bar;
	for (i = 0; i < conf->num_txq; i++) {
		drain_drv_to_sn_q(priv->inc_qs[i].drv_to_sn);
		drain_sn_to_drv_q(priv->inc_qs[i].sn_to_drv);
	}

	for (i = 0; i < conf->num_rxq; i++) {
		drain_drv_to_sn_q(priv->inc_qs[i].drv_to_sn);
		drain_sn_to_drv_q(priv->inc_qs[i].sn_to_drv);
	}

	rte_free(priv->bar);
}

static void *alloc_bar(struct port *p, int container_pid, int loopback)
{
	struct vport_priv *priv = get_port_priv(p);

	int bytes_per_llring;
	int total_bytes;
	
	void *bar;
	struct sn_conf_space *conf;
	char *ptr;
	
	int i;

	bytes_per_llring = llring_bytes_with_slots(SLOTS_PER_LLRING);

	total_bytes = sizeof(struct sn_conf_space);
	total_bytes += p->num_queues[PACKET_DIR_INC] * 2 * bytes_per_llring;
	total_bytes += p->num_queues[PACKET_DIR_OUT] * 
		(sizeof(struct sn_rxq_registers) + 2 * bytes_per_llring);
	
	bar = rte_zmalloc(NULL, total_bytes, 0);
	assert(bar);

	printf("vport_host_sndrv: allocated %d-byte BAR\n", total_bytes);

	conf = bar;

	conf->bar_size = total_bytes;
	conf->container_pid = container_pid;

	strncpy(conf->ifname, p->name, IFNAMSIZ);
	conf->ifname[IFNAMSIZ - 1] = '\0';

	memcpy(conf->mac_addr, p->mac_addr, ETH_ALEN);
	
	conf->num_txq = p->num_queues[PACKET_DIR_INC];
	conf->num_rxq = p->num_queues[PACKET_DIR_OUT];
	conf->link_on = 1;
	conf->promisc_on = 1;
	conf->loopback = loopback;

	ptr = (char *)(conf + 1);

	/* See sn_common.h for the llring usage */

	for (i = 0; i < conf->num_txq; i++) {
		/* Driver -> SoftNIC */
		llring_init((struct llring *)ptr, SLOTS_PER_LLRING,
				SINGLE_P, SINGLE_C);
		llring_set_water_mark((struct llring *)ptr, SLOTS_WATERMARK);
		priv->inc_qs[i].drv_to_sn = (struct llring *)ptr;
		ptr += bytes_per_llring;

		/* SoftNIC -> Driver */
		llring_init((struct llring *)ptr, SLOTS_PER_LLRING, 
				SINGLE_P, SINGLE_C);
		llring_set_water_mark((struct llring *)ptr, SLOTS_WATERMARK); 
		refill_tx_bufs((struct llring *)ptr, SLOTS_WATERMARK / 2);
		priv->inc_qs[i].sn_to_drv = (struct llring *)ptr;
		ptr += bytes_per_llring;
	}

	for (i = 0; i < conf->num_rxq; i++) {
		/* RX queue registers */
		priv->out_qs[i].rx_regs = (struct sn_rxq_registers *)ptr;
		ptr += sizeof(struct sn_rxq_registers);

		/* Driver -> SoftNIC */
		llring_init((struct llring *)ptr, SLOTS_PER_LLRING, 
				SINGLE_P, SINGLE_C);
		llring_set_water_mark((struct llring *)ptr, SLOTS_WATERMARK);
		priv->out_qs[i].drv_to_sn = (struct llring *)ptr;
		ptr += bytes_per_llring;

		/* SoftNIC -> Driver */
		llring_init((struct llring *)ptr, SLOTS_PER_LLRING, 
				SINGLE_P, SINGLE_C);
		llring_set_water_mark((struct llring *)ptr, SLOTS_WATERMARK);
		priv->out_qs[i].sn_to_drv = (struct llring *)ptr;
		ptr += bytes_per_llring;
	}

	return bar;
}

static int init_driver(struct driver *driver)
{
	struct stat buf;
	
	int ret;
	
	next_cpu = 0;

	ret = stat("/dev/softnic", &buf);
	if (ret < 0) {
		char exec_path[1024];
		char *exec_dir;
	
		char cmd[2048];

		fprintf(stderr, "vport: BESS kernel module is not " \
				"loaded. Loading...\n");

		ret = readlink("/proc/self/exe", exec_path, sizeof(exec_path));
		if (ret == -1 || ret >= sizeof(exec_path))
			return -errno;

		exec_path[ret] = '\0';
		exec_dir = dirname(exec_path);

		sprintf(cmd, "insmod %s/kmod/bess.ko", exec_dir);
		ret = system(cmd);
		if (WEXITSTATUS(ret) != 0)
			fprintf(stderr, "Warning: cannot load kernel" \
					"module %s/kmod/bess.ko\n", exec_dir);
	}

	return 0;
}

static struct snobj *docker_container_pid(char *cid, int *container_pid)
{
	char buf[1024];

	FILE *fp;
		
	int ret;
	int exit_code;

	if (!cid)
		return snobj_err(EINVAL, "field 'docker' should be " \
				"a containder ID or name in string");

	ret = snprintf(buf, sizeof(buf), 
			"docker inspect --format '{{.State.Pid}}' " \
			"%s 2>&1", cid);
	if (ret >= sizeof(buf))
		return snobj_err(EINVAL, "The specified Docker " \
				"container ID or name is too long");

	fp = popen(buf, "r");
	if (!fp) 
		return snobj_err_details(ESRCH, 
				snobj_str_fmt("popen() errno=%d (%s)",
					errno, strerror(errno)),
				"Command 'docker' is not available. " \
				"(not installed?)");

	ret = fread(buf, 1, sizeof(buf) - 1, fp);
	if (ret == 0)
		return snobj_err(ENOENT, "Cannot find the PID of " \
				"container %s", cid);

	buf[ret] = '\0';
	
	ret = pclose(fp);
	exit_code = WEXITSTATUS(ret);

	if (exit_code != 0 || sscanf(buf, "%d", container_pid) == 0) {
		struct snobj *details = snobj_map();

		snobj_map_set(details, "exit_code", snobj_int(exit_code));
		snobj_map_set(details, "docker_err", snobj_str(buf));
	
		return snobj_err_details(ESRCH, details,
				"Cannot find the PID of container %s", cid);
	}

	return NULL;
}

static int set_ip_addr_single(struct port *p, char *ip_addr)
{
	FILE *fp;

	char buf[1024];

	int ret;
	int exit_code;

	ret = snprintf(buf, sizeof(buf), "ip addr add %s dev %s 2>&1",
			ip_addr, p->name);
	if (ret >= sizeof(buf))
		return -EINVAL;

	fp = popen(buf, "r");
	if (!fp)
		return -errno;

	ret = pclose(fp);
	exit_code = WEXITSTATUS(ret);
	if (exit_code)
		return -EINVAL;

	return 0;
}

static struct snobj *set_ip_addr(struct port *p, int container_pid,
		struct snobj *arg)
{
	int child_pid;

	int ret = 0;

	if (snobj_type(arg) == TYPE_STR || snobj_type(arg) == TYPE_LIST) {
		if (snobj_type(arg) == TYPE_LIST) {
			if (arg->size == 0)
				goto invalid_type;

			for (int i = 0; i < arg->size; i++) {
				struct snobj *addr = snobj_list_get(arg, i);
				if (snobj_type(addr) != TYPE_STR)
					goto invalid_type;
			}
		}
	} else
		goto invalid_type;

	/* change network namespace if necessary */
	if (container_pid) {
		child_pid = fork();

		if (child_pid < 0)
			return snobj_errno(-child_pid);

		if (child_pid == 0) {
			char buf[1024];
			int fd;

			sprintf(buf, "/proc/%d/ns/net", container_pid);
			fd = open(buf, O_RDONLY);
			if (fd < 0) {
				perror("open(/proc/pid/ns/net)");
				exit(errno <= 255 ? errno: ENOMSG);
			}

			ret = setns(fd, 0);
			if (ret < 0) {
				perror("setns()");
				exit(errno <= 255 ? errno: ENOMSG);
			}
		} else
			goto wait_child;
	}

	switch (snobj_type(arg)) {
	case TYPE_STR: 
		ret = set_ip_addr_single(p, snobj_str_get(arg));
		if (ret < 0) {
			if (container_pid) {
				/* it must be the child */
				assert(child_pid == 0);	
				exit(errno <= 255 ? errno: ENOMSG);
			}
		}
		break;

	case TYPE_LIST: 
		if (!arg->size)
			goto invalid_type;

		for (int i = 0; i < arg->size; i++) {
			struct snobj *addr = snobj_list_get(arg, i);

			ret = set_ip_addr_single(p, snobj_str_get(addr));
			if (ret < 0) {
				if (container_pid) {
					/* it must be the child */
					assert(child_pid == 0);	
					exit(errno <= 255 ? errno: ENOMSG);
				} else
					break;
			}
		}

		break;

	default:
		assert(0);
	}

	if (container_pid) {
		if (child_pid == 0) {
			if (ret < 0) {
				ret = -ret;
				exit(ret <= 255 ? ret: ENOMSG);
			} else
				exit(0);
		} else {
			int exit_status;

wait_child:
			ret = waitpid(child_pid, &exit_status, 0);

			if (ret >= 0) {
				assert(ret == child_pid);
				ret = -WEXITSTATUS(exit_status);
			} else
				perror("waitpid()");
		}
	}

	if (ret < 0)
		return snobj_err_details(-ret, arg, 
				"Failed to set IP addresses " \
				"(incorrect IP address format?)");

	return NULL;

invalid_type:
	return snobj_err(EINVAL, "'ip_addr' must be a string or list " \
			"of IPv4/v6 addresses (e.g., '10.0.20.1/24')");
}

static void deinit_port(struct port *p)
{
	struct vport_priv *priv = get_port_priv(p);
	int ret;

	ret = ioctl(priv->fd, SN_IOC_RELEASE_HOSTNIC);
	if (ret < 0)
		perror("SN_IOC_RELEASE_HOSTNIC");	

	close(priv->fd);
	free_bar(priv);
}

static struct snobj *init_port(struct port *p, struct snobj *conf)
{
	struct vport_priv *priv = get_port_priv(p);

	int container_pid = 0;
	int cpu;
	int rxq;

	int ret;
	struct snobj *cpu_list = NULL;

	if (strlen(p->name) >= IFNAMSIZ)
		return snobj_err(EINVAL, "Linux interface name should be " \
				"shorter than %d characters", IFNAMSIZ);

	if (snobj_eval_exists(conf, "docker")) {
		struct snobj *err = docker_container_pid(
				snobj_eval_str(conf, "docker"), 
				&container_pid);

		if (err)
			return err;
	}

	if ((cpu_list = snobj_eval(conf, "rxq_cpus")) != NULL &&
	    cpu_list->size != p->num_queues[PACKET_DIR_OUT]) {
		return snobj_err(EINVAL, "Must specify as many cores as rxqs");
	}

	if (snobj_eval_exists(conf, "rxq_cpu") &&
	    p->num_queues[PACKET_DIR_OUT] > 1) {
	    return snobj_err(EINVAL, "Must specify as many cores as rxqs");
	}

	priv->fd = open("/dev/softnic", O_RDONLY);
	if (priv->fd == -1)
		return snobj_err(ENODEV, "the kernel module is not loaded");

	priv->bar = alloc_bar(p, container_pid, snobj_eval_int(conf, "loopback"));
	ret = ioctl(priv->fd, SN_IOC_CREATE_HOSTNIC, 
			rte_malloc_virt2phy(priv->bar));
	if (ret < 0) {
		close(priv->fd);
		return snobj_errno_details(-ret, 
				snobj_str("SN_IOC_CREATE_HOSTNIC failure"));
	}

	if (snobj_eval_exists(conf, "ip_addr")) {
		struct snobj *err = set_ip_addr(p, container_pid,
				snobj_eval(conf, "ip_addr"));
		
		if (err) {
			deinit_port(p);
			return err;
		}
	}

	for (cpu = 0; cpu < SN_MAX_CPU; cpu++)
		priv->map.cpu_to_txq[cpu] = 
			cpu % p->num_queues[PACKET_DIR_INC];

	if (cpu_list) {
		for (rxq = 0; rxq < p->num_queues[PACKET_DIR_OUT]; rxq++) {
			priv->map.rxq_to_cpu[rxq] = 
				snobj_int_get(snobj_list_get(cpu_list, rxq));
		}
	} else if (snobj_eval_exists(conf, "rxq_cpu")) {
		priv->map.rxq_to_cpu[0] = snobj_eval_int(conf, "rxq_cpu");
	} else {
		for (rxq = 0; rxq < p->num_queues[PACKET_DIR_OUT]; rxq++) {
			next_cpu = find_next_nonworker_cpu(next_cpu);
			priv->map.rxq_to_cpu[rxq] = next_cpu;
		}
	}

	ret = ioctl(priv->fd, SN_IOC_SET_QUEUE_MAPPING, &priv->map);
	if (ret < 0)
		perror("SN_IOC_SET_QUEUE_MAPPING");	

	return NULL;
}

static int get_tx_q(struct port *p, queue_t qid,
		    snb_array_t pkts, int max_cnt)
{
	struct vport_priv *priv = get_port_priv(p);
	struct queue *tx_queue = &priv->inc_qs[qid];
	void *objs[max_cnt];

	int cnt;
	int i;

	cnt = llring_dequeue_burst(tx_queue->drv_to_sn, 
			objs, max_cnt);
	if (cnt == 0)
		return 0;

	for (i = 0; i < cnt; i++) {
		pkts[i] = (struct snbuf *) objs[i];
		rte_prefetch0(snb_head_data(pkts[i]));
	}

	refill_tx_bufs(tx_queue->sn_to_drv, max_cnt);

	for (i = 0; i < cnt; i++) {
		struct sn_tx_metadata *tx_meta;
		int legit_size;

		tx_meta = (struct sn_tx_metadata *)snb_head_data(pkts[i]);

#if OLD_METADATA
		pkts[i]->in_port = vport->port.port_id;
		pkts[i]->in_queue = txq;

		/* TODO: sanity check for the metadata */
		pkts[i]->tx.csum_start = tx_meta->csum_start;
		pkts[i]->tx.csum_dest = tx_meta->csum_dest;
#endif

		legit_size = (snb_append(pkts[i], sizeof(struct sn_tx_metadata) + 
				tx_meta->length) != NULL);
		assert(legit_size);
		
		snb_adj(pkts[i], sizeof(struct sn_tx_metadata));
	}

	return cnt;
}

static int recv_pkts(struct port *p, queue_t qid, 
		snb_array_t pkts, int cnt)
{
	int ret = get_tx_q(p, qid, pkts, cnt);

	return ret;
}

/* returns nonzero if RX interrupt is needed */
static int put_rx_q(struct port *p, queue_t qid,
		    snb_array_t pkts, int cnt)
{
	struct vport_priv *priv = get_port_priv(p);
	struct queue *rx_queue = &priv->out_qs[qid];
	void *objs[SLOTS_PER_LLRING * 2];

	uint64_t bytes = 0;

	int ret;
	int i;

	for (i = 0; i < cnt; i++) {
		struct sn_rx_metadata *rx_meta;

		struct snbuf *pkt = pkts[i];
		struct rte_mbuf *mbuf;

		int total_len;

		total_len = snb_total_len(pkt);

		rx_meta = (struct sn_rx_metadata *)snb_head_data(pkt) - 1;

		rx_meta->length = total_len;

#if OLD_METADATA
		rx_meta->gso_mss = pkt->rx.gso_mss;
		rx_meta->csum_state = pkt->rx.csum_state;
#else
		rx_meta->gso_mss = 0;
		rx_meta->csum_state = SN_RX_CSUM_UNEXAMINED;
#endif
		rx_meta->host.seg_len = snb_head_len(pkt);
		rx_meta->host.seg_next = 0;

		for (mbuf = pkt->mbuf.next; mbuf; mbuf = mbuf->next) {
			struct sn_rx_metadata *next_rx_meta;

			next_rx_meta = rte_pktmbuf_mtod(mbuf, 
					struct sn_rx_metadata *) - 1;

			next_rx_meta->host.seg_len = rte_pktmbuf_data_len(mbuf);
			next_rx_meta->host.seg_next = 0;

			rx_meta->host.seg_next = snb_seg_dma_addr(mbuf) - 
				sizeof(struct sn_rx_metadata);
			rx_meta = next_rx_meta;
		}

		objs[i * 2 + 0] = (void *) pkt;
		objs[i * 2 + 1] = (void *) snb_dma_addr(pkt) - sizeof(struct sn_rx_metadata);

		bytes += total_len;
	}

	ret = llring_enqueue_bulk(rx_queue->sn_to_drv, 
			objs, cnt * 2);

	if (ret == -LLRING_ERR_NOBUF)
		return 0;

	/* TODO: generic notification architecture */
	if (__sync_bool_compare_and_swap(&rx_queue->rx_regs->irq_disabled,
				0, 1)) 
	{
		ret = ioctl(priv->fd, SN_IOC_KICK_RX, 
				1 << priv->map.rxq_to_cpu[qid]);
		if (ret)
			perror("ioctl_kick_rx");
	}

	/* TODO: defer this */
	/* Lazy deallocation of packet buffers */
	ret = llring_dequeue_burst(rx_queue->drv_to_sn, objs, 
			SLOTS_PER_LLRING);	
	if (ret > 0)
		snb_free_bulk((snb_array_t) objs, ret);

	return cnt;
}

static int send_pkts(struct port *p, queue_t qid, 
		snb_array_t pkts, int cnt)
{
	int ret = put_rx_q(p, qid, pkts, cnt);

	return ret;
}

static const struct driver vport_host = {
	.name 		= "VPort",
	.def_port_name	= "vport",
	.priv_size	= sizeof(struct vport_priv),
	.init_driver	= init_driver,
	.init_port 	= init_port,
	.deinit_port	= deinit_port,
	.recv_pkts 	= recv_pkts,
	.send_pkts 	= send_pkts,
};

ADD_DRIVER(vport_host)
