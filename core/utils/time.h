#ifndef BESS_UTILS_TIME_H_
#define BESS_UTILS_TIME_H_

#include <cstdint>
#include <cstdlib>
#include <ctime>

#include <sys/time.h>

extern uint64_t tsc_hz;

static inline uint64_t rdtsc(void) {
  uint32_t hi, lo;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return (uint64_t)lo | ((uint64_t)hi << 32);
}

static inline uint64_t tsc_to_ns(uint64_t cycles) {
  return cycles * 1000000000.0 / tsc_hz;
}

static inline double tsc_to_us(uint64_t cycles) {
  return cycles * 1000000.0 / tsc_hz;
}

/* Return current time in seconds since the Epoch.
 * This is consistent with Python's time.time() */
static inline double get_epoch_time() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

/* CPU time (in seconds) spent by the current thread.
 * Use it only relatively. */
static inline double get_cpu_time() {
  struct timespec ts;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
    return ts.tv_sec + ts.tv_nsec / 1e9;
  } else {
    return get_epoch_time();
  }
}

#endif  // BESS_UTILS_TIME_H_
