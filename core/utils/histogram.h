#ifndef BESS_UTILS_HISTOGRAM_H_
#define BESS_UTILS_HISTOGRAM_H_

#include "time.h"

#define HISTO_TIMEUNIT_MULT (1000lu * 1000 * 1000)  // Nano seconds
#define HISTO_TIME (100lu)       // We measure in 100 ns units
#define HISTO_BUCKETS (1000000)  // Buckets
#define HISTO_HARD_TIMEOUT (HISTO_BUCKETS)

#define HISTO_TIME_TO_SEC(t) ((t) / (HISTO_TIMEUNIT_MULT / HISTO_TIME))
#define PRINT_THRES (1)
#define HISTO_BUCKET_VAL(ptr) ((*(ptr)))
#define HISTO_BUCKET_INC(ptr) ((*(ptr))++)

#define HISTO_SUMMARY_POINTS (7)

typedef uint64_t histo_count_t;
struct histogram {
  histo_count_t arr[HISTO_BUCKETS];
  histo_count_t above_threshold;
};

struct hist_summary {
  uint64_t min;
  uint64_t avg;
  uint64_t max;
  uint64_t count;
  uint64_t latencies[HISTO_SUMMARY_POINTS];
};

static inline uint64_t get_time() {
  double t = rdtsc();
  return (uint64_t)(t * (HISTO_TIMEUNIT_MULT / HISTO_TIME) / tsc_hz);
}

static inline void record_latency(struct histogram* hist, uint64_t latency) {
  if (latency >= HISTO_HARD_TIMEOUT) {
    hist->above_threshold++;
  } else {
    histo_count_t* bucket = hist->arr + latency;
    HISTO_BUCKET_INC(bucket);
  }
}
void print_hist(struct histogram* hist);
struct histogram* combine_histograms(struct histogram* a, struct histogram* b);

// Dump summary statistics for "hist" into "summary". This is destructive.
void summarize_hist(struct histogram* hist, struct hist_summary *summary);
void print_summary(struct hist_summary* s);

// It's a good idea to call this after summarize_hist().
void reset_hist(struct histogram *hist);

#endif  // BESS_UTILS_HISTOGRAM_H_
