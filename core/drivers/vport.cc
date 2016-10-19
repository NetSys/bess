#include <fcntl.h>
#include <libgen.h>
#include <sched.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <rte_config.h>
#include <rte_malloc.h>

#include "../log.h"
#include "../kmod/sn_common.h"
#include "../port.h"

/* TODO: Unify vport and vport_native */

#define SLOTS_PER_LLRING 256

#define REFILL_LOW 16
#define REFILL_HIGH 32

/* This watermark is to detect congestion and cache bouncing due to
 * head-eating-tail (needs at least 8 slots less then the total ring slots).
 * Not sure how to tune this... */
#define SLOTS_WATERMARK ((SLOTS_PER_LLRING >> 3) * 7) /* 87.5% */

/* Disable (0) single producer/consumer mode for default */
#define SINGLE_P 0
#define SINGLE_C 0

/* We cannot directly use phys_addr_t on 32bit machines,
 * since it may be sizeof(phys_addr_t) != sizeof(void *) */
typedef uintptr_t paddr_t;

struct queue {
  union {
    struct sn_rxq_registers *rx_regs;
  };

  struct llring *drv_to_sn;
  struct llring *sn_to_drv;
};

static inline int find_next_nonworker_cpu(int cpu) {
  do {
    cpu = (cpu + 1) % sysconf(_SC_NPROCESSORS_ONLN);
  } while (is_worker_core(cpu));
  return cpu;
}

static void refill_tx_bufs(struct llring *r) {
  struct snbuf *pkts[REFILL_HIGH];
  void *objs[REFILL_HIGH];

  int deficit;
  int ret;

  int curr_cnt = llring_count(r);

  if (curr_cnt >= REFILL_LOW) return;

  deficit = REFILL_HIGH - curr_cnt;

  ret = snb_alloc_bulk((snb_array_t)pkts, deficit, 0);
  if (ret == 0) return;

  for (int i = 0; i < ret; i++)
    objs[i] = (void *)(uintptr_t)snb_to_paddr(pkts[i]);

  ret = llring_mp_enqueue_bulk(r, objs, ret);
  assert(ret == 0);
}

static void drain_sn_to_drv_q(struct llring *q) {
  /* sn_to_drv queues contain physical address of packet buffers */
  for (;;) {
    paddr_t paddr;
    struct snbuf *snb;
    int ret;

    ret = llring_mc_dequeue(q, (void **)&paddr);
    if (ret) break;

    snb = paddr_to_snb(paddr);
    if (!snb) {
      log_err("paddr_to_snb(%" PRIxPTR ") failed\n", paddr);
      continue;
    }

    snb_free(paddr_to_snb(paddr));
  }
}

static void drain_drv_to_sn_q(struct llring *q) {
  /* sn_to_drv queues contain virtual address of packet buffers */
  for (;;) {
    struct snbuf *snb;
    int ret;

    ret = llring_mc_dequeue(q, (void **)&snb);
    if (ret) break;

    snb_free(snb);
  }
}

static void reclaim_packets(struct llring *ring) {
  void *objs[MAX_PKT_BURST];
  int ret;

  for (;;) {
    ret = llring_mc_dequeue_burst(ring, objs, MAX_PKT_BURST);
    if (ret == 0) break;

    snb_free_bulk((snb_array_t)objs, ret);
  }
}

static struct snobj *docker_container_pid(char *cid, int *container_pid) {
  char buf[1024];

  FILE *fp;

  int ret;
  int exit_code;

  if (!cid)
    return snobj_err(EINVAL,
                     "field 'docker' should be "
                     "a containder ID or name in string");

  ret = snprintf(buf, static_cast<int>(sizeof(buf)),
                 "docker inspect --format '{{.State.Pid}}' "
                 "%s 2>&1",
                 cid);
  if (ret >= static_cast<int>(sizeof(buf)))
    return snobj_err(EINVAL,
                     "The specified Docker "
                     "container ID or name is too long");

  fp = popen(buf, "r");
  if (!fp)
    return snobj_err_details(
        ESRCH, snobj_str_fmt("popen() errno=%d (%s)", errno, strerror(errno)),
        "Command 'docker' is not available. "
        "(not installed?)");

  ret = fread(buf, 1, sizeof(buf) - 1, fp);
  if (ret == 0)
    return snobj_err(ENOENT,
                     "Cannot find the PID of "
                     "container %s",
                     cid);

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

class VPort : public Port {
 public:
  virtual void InitDriver();

  struct snobj *Init(struct snobj *conf);
  void DeInit();

  int RecvPackets(queue_t qid, snb_array_t pkts, int max_cnt);
  int SendPackets(queue_t qid, snb_array_t pkts, int cnt);

 private:
  void FreeBar();
  void *AllocBar(struct tx_queue_opts *txq_opts,
                 struct rx_queue_opts *rxq_opts);
  int SetIPAddrSingle(char *ip_addr);
  struct snobj *SetIPAddr(struct snobj *arg);

  int fd_ = {0};

  char ifname_[IFNAMSIZ] = {{0}}; /* could be different from Name() */
  void *bar_ = {0};

  struct queue inc_qs_[MAX_QUEUES_PER_DIR];
  struct queue out_qs_[MAX_QUEUES_PER_DIR];

  struct sn_ioc_queue_mapping map_ = {{0}};

  int netns_fd_ = {0};
  int container_pid_ = {0};
};

static int next_cpu;

/* Free an allocated bar, freeing resources in the queues */
void VPort::FreeBar() {
  int i;
  struct sn_conf_space *conf = static_cast<struct sn_conf_space *>(bar_);

  for (i = 0; i < conf->num_txq; i++) {
    drain_drv_to_sn_q(inc_qs_[i].drv_to_sn);
    drain_sn_to_drv_q(inc_qs_[i].sn_to_drv);
  }

  for (i = 0; i < conf->num_rxq; i++) {
    drain_drv_to_sn_q(inc_qs_[i].drv_to_sn);
    drain_sn_to_drv_q(inc_qs_[i].sn_to_drv);
  }

  rte_free(bar_);
}

void *VPort::AllocBar(struct tx_queue_opts *txq_opts,
                      struct rx_queue_opts *rxq_opts) {
  int bytes_per_llring;
  int total_bytes;

  void *bar;
  struct sn_conf_space *conf;
  char *ptr;

  int i;

  bytes_per_llring = llring_bytes_with_slots(SLOTS_PER_LLRING);

  total_bytes = sizeof(struct sn_conf_space);
  total_bytes += num_queues[PACKET_DIR_INC] * 2 * bytes_per_llring;
  total_bytes += num_queues[PACKET_DIR_OUT] *
                 (sizeof(struct sn_rxq_registers) + 2 * bytes_per_llring);

  log_err("total_bytes = %d\n", total_bytes);
  bar = rte_zmalloc(NULL, total_bytes, 0);
  assert(bar);

  /* log_debug("vport_host_sndrv: allocated %dB BAR\n", total_bytes); */

  conf = reinterpret_cast<struct sn_conf_space *>(bar);

  conf->bar_size = total_bytes;
  conf->netns_fd = netns_fd_;
  conf->container_pid = container_pid_;

  strcpy(conf->ifname, ifname_);

  memcpy(conf->mac_addr, mac_addr, ETH_ALEN);

  conf->num_txq = num_queues[PACKET_DIR_INC];
  conf->num_rxq = num_queues[PACKET_DIR_OUT];
  conf->link_on = 1;
  conf->promisc_on = 1;

  conf->txq_opts = *txq_opts;
  conf->rxq_opts = *rxq_opts;

  ptr = (char *)(conf + 1);

  /* See sn_common.h for the llring usage */

  for (i = 0; i < conf->num_txq; i++) {
    /* Driver -> BESS */
    llring_init((struct llring *)ptr, SLOTS_PER_LLRING, SINGLE_P, SINGLE_C);
    inc_qs_[i].drv_to_sn = (struct llring *)ptr;
    ptr += bytes_per_llring;

    /* BESS -> Driver */
    llring_init((struct llring *)ptr, SLOTS_PER_LLRING, SINGLE_P, SINGLE_C);
    refill_tx_bufs((struct llring *)ptr);
    inc_qs_[i].sn_to_drv = (struct llring *)ptr;
    ptr += bytes_per_llring;
  }

  for (i = 0; i < conf->num_rxq; i++) {
    /* RX queue registers */
    out_qs_[i].rx_regs = (struct sn_rxq_registers *)ptr;
    ptr += sizeof(struct sn_rxq_registers);

    /* Driver -> BESS */
    llring_init((struct llring *)ptr, SLOTS_PER_LLRING, SINGLE_P, SINGLE_C);
    out_qs_[i].drv_to_sn = (struct llring *)ptr;
    ptr += bytes_per_llring;

    /* BESS -> Driver */
    llring_init((struct llring *)ptr, SLOTS_PER_LLRING, SINGLE_P, SINGLE_C);
    out_qs_[i].sn_to_drv = (struct llring *)ptr;
    ptr += bytes_per_llring;
  }

  return bar;
}

void VPort::InitDriver() {
  struct stat buf;

  int ret;

  next_cpu = 0;

  ret = stat("/dev/bess", &buf);
  if (ret < 0) {
    char exec_path[1024];
    char *exec_dir;

    char cmd[2048];

    log_notice(
        "vport: BESS kernel module is not loaded. "
        "Loading...\n");

    ret = readlink("/proc/self/exe", exec_path, sizeof(exec_path));
    if (ret == -1 || ret >= static_cast<int>(sizeof(exec_path))) return;

    exec_path[ret] = '\0';
    exec_dir = dirname(exec_path);

    sprintf(cmd, "insmod %s/kmod/bess.ko", exec_dir);
    ret = system(cmd);
    if (WEXITSTATUS(ret) != 0)
      log_err(
          "Warning: cannot load kernel"
          "module %s/kmod/bess.ko\n",
          exec_dir);
  }
}

int VPort::SetIPAddrSingle(char *ip_addr) {
  FILE *fp;

  char buf[1024];

  int ret;
  int exit_code;

  ret = snprintf(buf, sizeof(buf), "ip addr add %s dev %s 2>&1", ip_addr,
                 ifname_);
  if (ret >= static_cast<int>(sizeof(buf))) return -EINVAL;

  fp = popen(buf, "r");
  if (!fp) return -errno;

  ret = pclose(fp);
  exit_code = WEXITSTATUS(ret);
  if (exit_code) return -EINVAL;

  return 0;
}

struct snobj *VPort::SetIPAddr(struct snobj *arg) {
  int child_pid;

  int ret = 0;
  int nspace = 0;

  if (snobj_type(arg) == TYPE_STR || snobj_type(arg) == TYPE_LIST) {
    if (snobj_type(arg) == TYPE_LIST) {
      if (arg->size == 0) goto invalid_type;

      for (size_t i = 0; i < arg->size; i++) {
        struct snobj *addr = snobj_list_get(arg, i);
        if (snobj_type(addr) != TYPE_STR) goto invalid_type;
      }
    }
  } else
    goto invalid_type;

  /* change network namespace if necessary */
  if (container_pid_ || netns_fd_ >= 0) {
    nspace = 1;

    child_pid = fork();
    if (child_pid < 0) return snobj_errno(-child_pid);

    if (child_pid == 0) {
      char buf[1024];
      int fd;

      if (container_pid_) {
        sprintf(buf, "/proc/%d/ns/net", container_pid_);
        fd = open(buf, O_RDONLY);
        if (fd < 0) {
          log_perr("open(/proc/pid/ns/net)");
          exit(errno <= 255 ? errno : ENOMSG);
        }
      } else
        fd = netns_fd_;

      ret = setns(fd, 0);
      if (ret < 0) {
        log_perr("setns()");
        exit(errno <= 255 ? errno : ENOMSG);
      }
    } else
      goto wait_child;
  }

  switch (snobj_type(arg)) {
    case TYPE_STR:
      ret = SetIPAddrSingle(snobj_str_get(arg));
      if (ret < 0) {
        if (nspace) {
          /* it must be the child */
          assert(child_pid == 0);
          exit(errno <= 255 ? errno : ENOMSG);
        }
      }
      break;

    case TYPE_LIST:
      if (!arg->size) goto invalid_type;

      for (size_t i = 0; i < arg->size; i++) {
        struct snobj *addr = snobj_list_get(arg, i);

        ret = SetIPAddrSingle(snobj_str_get(addr));
        if (ret < 0) {
          if (nspace) {
            /* it must be the child */
            assert(child_pid == 0);
            exit(errno <= 255 ? errno : ENOMSG);
          } else
            break;
        }
      }

      break;

    default:
      assert(0);
  }

  if (nspace) {
    if (child_pid == 0) {
      if (ret < 0) {
        ret = -ret;
        exit(ret <= 255 ? ret : ENOMSG);
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
        log_perr("waitpid()");
    }
  }

  if (ret < 0)
    return snobj_err(-ret,
                     "Failed to set IP addresses "
                     "(incorrect IP address format?)");

  return NULL;

invalid_type:
  return snobj_err(EINVAL,
                   "'ip_addr' must be a string or list "
                   "of IPv4/v6 addresses (e.g., '10.0.20.1/24')");
}

void VPort::DeInit() {
  int ret;

  ret = ioctl(fd_, SN_IOC_RELEASE_HOSTNIC);
  if (ret < 0) log_perr("SN_IOC_RELEASE_HOSTNIC");

  close(fd_);
  FreeBar();
}

struct snobj *VPort::Init(struct snobj *conf) {
  int cpu;
  int rxq;

  int ret;

  struct snobj *cpu_list = NULL;

  const char *netns;
  const char *ifname;

  struct snobj *err = NULL;

  struct tx_queue_opts txq_opts = {0};
  struct rx_queue_opts rxq_opts = {0};

  fd_ = -1;
  netns_fd_ = -1;
  container_pid_ = 0;

  ifname = snobj_eval_str(conf, "ifname");
  if (!ifname) ifname = name().c_str();

  if (strlen(ifname) >= IFNAMSIZ) {
    err = snobj_err(EINVAL,
                    "Linux interface name should be "
                    "shorter than %d characters",
                    IFNAMSIZ);
    goto fail;
  }

  strcpy(ifname_, ifname);

  if (snobj_eval_exists(conf, "docker")) {
    err = docker_container_pid(snobj_eval_str(conf, "docker"), &container_pid_);

    if (err) goto fail;
  }

  if (snobj_eval_exists(conf, "container_pid")) {
    if (container_pid_) {
      err = snobj_err(EINVAL,
                      "You cannot specify both "
                      "'docker' and 'container_pid'");
      goto fail;
    }

    container_pid_ = snobj_eval_int(conf, "container_pid");
  }

  if ((netns = snobj_eval_str(conf, "netns"))) {
    if (container_pid_)
      return snobj_err(EINVAL,
                       "You should specify only "
                       "one of 'docker', 'container_pid', "
                       "or 'netns'");

    netns_fd_ = open(netns, O_RDONLY);
    if (netns_fd_ < 0) {
      err = snobj_err(EINVAL, "Invalid network namespace %s", netns);
      goto fail;
    }
  }

  if ((cpu_list = snobj_eval(conf, "rxq_cpus")) != NULL &&
      cpu_list->size != num_queues[PACKET_DIR_OUT]) {
    err = snobj_err(EINVAL, "Must specify as many cores as rxqs");
    goto fail;
  }

  if (snobj_eval_exists(conf, "rxq_cpu") && num_queues[PACKET_DIR_OUT] > 1) {
    err = snobj_err(EINVAL, "Must specify as many cores as rxqs");
    goto fail;
  }

  fd_ = open("/dev/bess", O_RDONLY);
  if (fd_ == -1) {
    err = snobj_err(ENODEV, "the kernel module is not loaded");
    goto fail;
  }

  txq_opts.tci = snobj_eval_uint(conf, "tx_tci");
  txq_opts.outer_tci = snobj_eval_uint(conf, "tx_outer_tci");
  rxq_opts.loopback = snobj_eval_uint(conf, "loopback");

  bar_ = AllocBar(&txq_opts, &rxq_opts);

  log_err("%p %" PRIx64 "\n", bar_, rte_malloc_virt2phy(bar_));
  ret = ioctl(fd_, SN_IOC_CREATE_HOSTNIC, rte_malloc_virt2phy(bar_));
  if (ret < 0) {
    err = snobj_errno_details(-ret, snobj_str("SN_IOC_CREATE_HOSTNIC failure"));
    goto fail;
  }

  if (snobj_eval_exists(conf, "ip_addr")) {
    err = SetIPAddr(snobj_eval(conf, "ip_addr"));

    if (err) {
      DeInit();
      goto fail;
    }
  }

  if (netns_fd_ >= 0) {
    close(netns_fd_);
    netns_fd_ = -1;
  }

  for (cpu = 0; cpu < SN_MAX_CPU; cpu++)
    map_.cpu_to_txq[cpu] = cpu % num_queues[PACKET_DIR_INC];

  if (cpu_list) {
    for (rxq = 0; rxq < num_queues[PACKET_DIR_OUT]; rxq++) {
      map_.rxq_to_cpu[rxq] = snobj_int_get(snobj_list_get(cpu_list, rxq));
    }
  } else if (snobj_eval_exists(conf, "rxq_cpu")) {
    map_.rxq_to_cpu[0] = snobj_eval_int(conf, "rxq_cpu");
  } else {
    for (rxq = 0; rxq < num_queues[PACKET_DIR_OUT]; rxq++) {
      next_cpu = find_next_nonworker_cpu(next_cpu);
      map_.rxq_to_cpu[rxq] = next_cpu;
    }
  }

  ret = ioctl(fd_, SN_IOC_SET_QUEUE_MAPPING, &map_);
  if (ret < 0) log_perr("ioctl(SN_IOC_SET_QUEUE_MAPPING)");

  return NULL;

fail:
  if (fd_ >= 0) close(fd_);

  if (netns_fd_ >= 0) close(netns_fd_);

  return err;
}

int VPort::RecvPackets(queue_t qid, snb_array_t pkts, int max_cnt) {
  struct queue *tx_queue = &inc_qs_[qid];

  int cnt;
  int i;

  cnt = llring_sc_dequeue_burst(tx_queue->drv_to_sn, (void **)pkts, max_cnt);

  refill_tx_bufs(tx_queue->sn_to_drv);

  for (i = 0; i < cnt; i++) {
    struct snbuf *pkt = pkts[i];
    struct sn_tx_desc *tx_desc;
    uint16_t len;

    tx_desc = (struct sn_tx_desc *)pkt->_scratchpad;
    len = tx_desc->total_len;

    pkt->mbuf.data_off = SNBUF_HEADROOM;
    pkt->mbuf.pkt_len = len;
    pkt->mbuf.data_len = len;

    /* TODO: process sn_tx_metadata */
  }

  return cnt;
}

int VPort::SendPackets(queue_t qid, snb_array_t pkts, int cnt) {
  struct queue *rx_queue = &out_qs_[qid];

  paddr_t paddr[MAX_PKT_BURST];

  int ret;

  reclaim_packets(rx_queue->drv_to_sn);

  for (int i = 0; i < cnt; i++) {
    struct snbuf *snb = pkts[i];

    struct sn_rx_desc *rx_desc;

    rx_desc = (struct sn_rx_desc *)snb->_scratchpad;

    rte_prefetch0(rx_desc);

    paddr[i] = snb_to_paddr(snb);
  }

  for (int i = 0; i < cnt; i++) {
    struct snbuf *snb = pkts[i];
    struct rte_mbuf *mbuf = &snb->mbuf;

    struct sn_rx_desc *rx_desc;

    rx_desc = (struct sn_rx_desc *)snb->_scratchpad;

    rx_desc->total_len = snb_total_len(snb);
    rx_desc->seg_len = snb_head_len(snb);
    rx_desc->seg = snb_dma_addr(snb);
    rx_desc->next = 0;

    rx_desc->meta = {};

    for (struct rte_mbuf *seg = mbuf->next; seg; seg = seg->next) {
      struct sn_rx_desc *next_desc;
      struct snbuf *seg_snb;

      seg_snb = (struct snbuf *)seg;
      next_desc = (struct sn_rx_desc *)seg_snb->_scratchpad;

      next_desc->seg_len = rte_pktmbuf_data_len(seg);
      next_desc->seg = snb_seg_dma_addr(seg);
      next_desc->next = 0;

      rx_desc->next = snb_to_paddr(seg_snb);
      rx_desc = next_desc;
    }
  }

  ret = llring_mp_enqueue_bulk(rx_queue->sn_to_drv, (void **)paddr, cnt);

  if (ret == -LLRING_ERR_NOBUF) return 0;

  /* TODO: generic notification architecture */
  if (__sync_bool_compare_and_swap(&rx_queue->rx_regs->irq_disabled, 0, 1)) {
    ret = ioctl(fd_, SN_IOC_KICK_RX, 1 << map_.rxq_to_cpu[qid]);
    if (ret) log_perr("ioctl(kick_rx)");
  }

  return cnt;
}

ADD_DRIVER(VPort, "vport", "Virtual port for Linux host")
