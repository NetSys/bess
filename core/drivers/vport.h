#ifndef BESS_DRIVERS_VPORT_H_
#define BESS_DRIVERS_VPORT_H_

#include "../kmod/sn_common.h"
#include "../port.h"

class VPort : public Port {
 public:
  VPort() : fd_(), bar_(), map_(), netns_fd_(), container_pid_() {}
  virtual void InitDriver();

  pb_error_t InitPb(const bess::pb::VPortArg &arg);
  struct snobj *Init(struct snobj *conf);
  void DeInit();

  int RecvPackets(queue_t qid, snb_array_t pkts, int max_cnt);
  int SendPackets(queue_t qid, snb_array_t pkts, int cnt);

 private:
  struct queue {
    union {
      struct sn_rxq_registers *rx_regs;
    };

    struct llring *drv_to_sn;
    struct llring *sn_to_drv;
  };

  void FreeBar();
  void *AllocBar(struct tx_queue_opts *txq_opts,
                 struct rx_queue_opts *rxq_opts);
  int SetIPAddrSingle(const std::string &ip_addr);
  pb_error_t SetIPAddr(const bess::pb::VPortArg &arg);
  struct snobj *SetIPAddr(struct snobj *arg);

  int fd_;

  char ifname_[IFNAMSIZ]; /* could be different from Name() */
  void *bar_;

  struct queue inc_qs_[MAX_QUEUES_PER_DIR];
  struct queue out_qs_[MAX_QUEUES_PER_DIR];

  struct sn_ioc_queue_mapping map_;

  int netns_fd_;
  int container_pid_;
};

#endif  // BESS_DRIVERS_VPORT_H_
