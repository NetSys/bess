#include "scheduler.h"

#include "opts.h"
#include "traffic_class.h"
#include "utils/common.h"
#include "worker.h"

namespace bess {

void Scheduler::ScheduleLoop() {
  uint64_t now;
  // How many rounds to go before we do accounting.
  const uint64_t accounting_mask = 0xff;
  static_assert(((accounting_mask + 1) & accounting_mask) == 0,
                "Accounting mask must be (2^n)-1");

  checkpoint_ = now = rdtsc();

  // The main scheduling, running, accounting loop.
  for (uint64_t round = 0;; ++round) {
    // Periodic check, to mitigate expensive operations.
    if ((round & accounting_mask) == 0) {
      if (ctx.is_pause_requested()) {
        if (ctx.BlockWorker()) {
          break;
        }
      }
    }

    ScheduleOnce();
  }
}

}  // namespace bess
