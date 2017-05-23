#include "queue.h"

#include "../mem_alloc.h"
#include "../utils/format.h"

#define DEFAULT_QUEUE_SIZE 1024

const Commands Queue::cmds = {
    {"set_burst", "QueueCommandSetBurstArg",
     MODULE_CMD_FUNC(&Queue::CommandSetBurst), 1},
    {"set_size", "QueueCommandSetSizeArg",
     MODULE_CMD_FUNC(&Queue::CommandSetSize), 0},
};

int Queue::Resize(int slots) {
  struct llring *old_queue = queue_;
  struct llring *new_queue;

  int bytes = llring_bytes_with_slots(slots);

  int ret;

  new_queue = static_cast<llring *>(mem_alloc_ex(bytes, alignof(llring), 0));
  if (!new_queue) {
    return -ENOMEM;
  }

  ret = llring_init(new_queue, slots, 0, 1);
  if (ret) {
    mem_free(new_queue);
    return -EINVAL;
  }

  /* migrate packets from the old queue */
  if (old_queue) {
    bess::Packet *pkt;

    while (llring_sc_dequeue(old_queue, (void **)&pkt) == 0) {
      ret = llring_sp_enqueue(new_queue, pkt);
      if (ret == -LLRING_ERR_NOBUF) {
        bess::Packet::Free(pkt);
      }
    }

    mem_free(old_queue);
  }

  queue_ = new_queue;

  return 0;
}

CommandResponse Queue::Init(const bess::pb::QueueArg &arg) {
  task_id_t tid;
  CommandResponse err;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID) {
    return CommandFailure(ENOMEM, "Task creation failed");
  }

  burst_ = bess::PacketBatch::kMaxBurst;

  if (arg.size() != 0) {
    err = SetSize(arg.size());
    if (err.error().code() != 0) {
      return err;
    }
  } else {
    int ret = Resize(DEFAULT_QUEUE_SIZE);
    if (ret) {
      return CommandFailure(-ret);
    }
  }

  if (arg.prefetch()) {
    prefetch_ = true;
  }

  return CommandSuccess();
}

void Queue::DeInit() {
  bess::Packet *pkt;

  if (queue_) {
    while (llring_sc_dequeue(queue_, (void **)&pkt) == 0) {
      bess::Packet::Free(pkt);
    }
    mem_free(queue_);
  }
}

std::string Queue::GetDesc() const {
  const struct llring *ring = queue_;

  return bess::utils::Format("%u/%u", llring_count(ring), ring->common.slots);
}

/* from upstream */
void Queue::ProcessBatch(bess::PacketBatch *batch) {
  int queued =
      llring_mp_enqueue_burst(queue_, (void **)batch->pkts(), batch->cnt());

  if (queued < batch->cnt()) {
    bess::Packet::Free(batch->pkts() + queued, batch->cnt() - queued);
  }
}

/* to downstream */
struct task_result Queue::RunTask(void *) {
  bess::PacketBatch batch;
  struct task_result ret;

  const int burst = ACCESS_ONCE(burst_);
  const int pkt_overhead = 24;

  uint64_t total_bytes = 0;

  uint64_t cnt = llring_sc_dequeue_burst(queue_, (void **)batch.pkts(), burst);

  if (cnt > 0) {
    batch.set_cnt(cnt);
    RunNextModule(&batch);
  }

  if (prefetch_) {
    for (uint64_t i = 0; i < cnt; i++) {
      total_bytes += batch.pkts()[i]->total_len();
      rte_prefetch0(batch.pkts()[i]->head_data());
    }
  } else {
    for (uint64_t i = 0; i < cnt; i++)
      total_bytes += batch.pkts()[i]->total_len();
  }

  ret = (struct task_result){
      .packets = cnt, .bits = (total_bytes + cnt * pkt_overhead) * 8,
  };

  return ret;
}

CommandResponse Queue::CommandSetBurst(
    const bess::pb::QueueCommandSetBurstArg &arg) {
  uint64_t burst = arg.burst();

  if (burst > bess::PacketBatch::kMaxBurst) {
    return CommandFailure(EINVAL, "burst size must be [0,%zu]",
                          bess::PacketBatch::kMaxBurst);
  }

  burst_ = burst;
  return CommandSuccess();
}

CommandResponse Queue::SetSize(uint64_t size) {
  if (size < 4 || size > 16384) {
    return CommandFailure(EINVAL, "must be in [4, 16384]");
  }

  if (size & (size - 1)) {
    return CommandFailure(EINVAL, "must be a power of 2");
  }

  int ret = Resize(size);
  if (ret) {
    return CommandFailure(-ret);
  }
  return CommandSuccess();
}

CommandResponse Queue::CommandSetSize(
    const bess::pb::QueueCommandSetSizeArg &arg) {
  return SetSize(arg.size());
}

CheckConstraintResult Queue::CheckModuleConstraints() const {
  CheckConstraintResult status = CHECK_OK;
  if (num_active_tasks() - tasks().size() < 1) {  // Assume multi-producer.
    LOG(ERROR) << "Queue has no producers";
    status = CHECK_NONFATAL_ERROR;
  }

  if (tasks().size() > 1) {  // Assume single consumer.
    LOG(ERROR) << "More than one consumer for the queue" << name();
    return CHECK_FATAL_ERROR;
  }

  return status;
}

ADD_MODULE(Queue, "queue",
           "terminates current task and enqueue packets for new task")
