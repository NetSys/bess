#include "time.h"

#include <unistd.h>

uint64_t tsc_hz;

namespace {

class TscHzSetter {
 public:
  TscHzSetter() {
    // TODO (sangjin): Make this faster and more accurate
    // (e.g., by using CLOCK_MONOTONIC_RAW)
    uint64_t start = rdtsc();
    usleep(100000);  // 0.1 sec
    tsc_hz = (rdtsc() - start) * 10;
  }
} _dummy;

}  // namespace (unnamed)
