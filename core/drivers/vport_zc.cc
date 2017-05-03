#include "vport_zc.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <rte_config.h>
#include <rte_malloc.h>

#define ROUND_TO_64(x) ((x + 32) & (~0x3f))

CommandResponse ZeroCopyVPort::Init(const bess::pb::EmptyArg &) {
  struct vport_bar *bar = nullptr;

  int num_inc_q = num_queues[PACKET_DIR_INC];
  int num_out_q = num_queues[PACKET_DIR_OUT];

  int bytes_per_llring;
  int total_bytes;
  uint8_t *ptr;
  int i;
  char port_dir[PORT_NAME_LEN + 256];
  char file_name[PORT_NAME_LEN + 256];
  struct stat sb;
  FILE *fp;
  size_t bar_address;

  bytes_per_llring = llring_bytes_with_slots(SLOTS_PER_LLRING);
  total_bytes = ROUND_TO_64(sizeof(struct vport_bar)) +
                ROUND_TO_64(bytes_per_llring) * (num_inc_q + num_out_q) +
                ROUND_TO_64(sizeof(struct vport_inc_regs)) * num_inc_q +
                ROUND_TO_64(sizeof(struct vport_out_regs)) * num_out_q;

  bar = static_cast<struct vport_bar *>(rte_zmalloc(nullptr, total_bytes, 64));
  bar_address = (size_t)bar;
  DCHECK(bar);
  bar_ = bar;

  strncpy(bar->name, name().c_str(), PORT_NAME_LEN);
  bar->num_inc_q = num_inc_q;
  bar->num_out_q = num_out_q;

  ptr = (uint8_t *)(bar);
  ptr += ROUND_TO_64(sizeof(struct vport_bar));

  /* Set up inc llrings */
  for (i = 0; i < num_inc_q; i++) {
    inc_regs_[i] = bar->inc_regs[i] =
        reinterpret_cast<struct vport_inc_regs *>(ptr);
    ptr += ROUND_TO_64(sizeof(struct vport_inc_regs));

    llring_init(reinterpret_cast<struct llring *>(ptr), SLOTS_PER_LLRING,
                SINGLE_P, SINGLE_C);
    llring_set_water_mark(reinterpret_cast<struct llring *>(ptr),
                          SLOTS_WATERMARK);
    bar->inc_qs[i] = reinterpret_cast<struct llring *>(ptr);
    inc_qs_[i] = bar->inc_qs[i];
    ptr += ROUND_TO_64(bytes_per_llring);
  }

  /* Set up out llrings */
  for (i = 0; i < num_out_q; i++) {
    out_regs_[i] = bar->out_regs[i] =
        reinterpret_cast<struct vport_out_regs *>(ptr);
    ptr += ROUND_TO_64(sizeof(struct vport_out_regs));

    llring_init(reinterpret_cast<struct llring *>(ptr), SLOTS_PER_LLRING,
                SINGLE_P, SINGLE_C);
    llring_set_water_mark(reinterpret_cast<struct llring *>(ptr),
                          SLOTS_WATERMARK);
    bar->out_qs[i] = reinterpret_cast<struct llring *>(ptr);
    out_qs_[i] = bar->out_qs[i];
    ptr += ROUND_TO_64(bytes_per_llring);
  }

  snprintf(port_dir, PORT_NAME_LEN + 256, "%s/%s", P_tmpdir, VPORT_DIR_PREFIX);

  if (stat(port_dir, &sb) == 0) {
    DCHECK_EQ((sb.st_mode & S_IFMT), S_IFDIR);
  } else {
    LOG(INFO) << "Creating directory " << port_dir;
    mkdir(port_dir, S_IRWXU | S_IRWXG | S_IRWXO);
  }

  for (i = 0; i < num_out_q; i++) {
    snprintf(file_name, PORT_NAME_LEN + 256, "%s/%s/%s.rx%d", P_tmpdir,
             VPORT_DIR_PREFIX, name().c_str(), i);

    mkfifo(file_name, 0666);

    out_irq_fd_[i] = open(file_name, O_RDWR);
  }

  snprintf(file_name, PORT_NAME_LEN + 256, "%s/%s/%s", P_tmpdir,
           VPORT_DIR_PREFIX, name().c_str());
  LOG(INFO) << "Writing port information to " << file_name;
  fp = fopen(file_name, "w");
  fwrite(&bar_address, 8, 1, fp);
  fclose(fp);

  return CommandSuccess();
}

void ZeroCopyVPort::DeInit() {
  char file_name[PORT_NAME_LEN + 256];

  int num_out_q = num_queues[PACKET_DIR_OUT];

  for (int i = 0; i < num_out_q; i++) {
    snprintf(file_name, PORT_NAME_LEN + 256, "%s/%s/%s.rx%d", P_tmpdir,
             VPORT_DIR_PREFIX, name().c_str(), i);

    unlink(file_name);
    close(out_irq_fd_[i]);
  }

  snprintf(file_name, PORT_NAME_LEN + 256, "%s/%s/%s", P_tmpdir,
           VPORT_DIR_PREFIX, name().c_str());
  unlink(file_name);

  rte_free(bar_);
}

int ZeroCopyVPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  struct llring *q = out_qs_[qid];
  int ret;

  ret = llring_enqueue_bulk(q, (void **)pkts, cnt);
  if (ret == -LLRING_ERR_NOBUF)
    return 0;

  if (__sync_bool_compare_and_swap(&out_regs_[qid]->irq_enabled, 1, 0)) {
    char t[1] = {'F'};
    ret = write(out_irq_fd_[qid], reinterpret_cast<void *>(t), 1);
  }

  return cnt;
}

int ZeroCopyVPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  struct llring *q = inc_qs_[qid];
  int ret;

  ret = llring_dequeue_burst(q, (void **)pkts, cnt);
  return ret;
}

ADD_DRIVER(ZeroCopyVPort, "zcvport",
           "zero copy virtual port for trusted user apps")
