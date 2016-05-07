#ifndef __HISTOGRAM_H__
#define __HISTOGRAM_H__

#include <rte_cycles.h>

#include "../mem_alloc.h"

#define HISTO_TIMEUNIT_MULT (1000lu*1000*1000) // Nano seconds
#define HISTO_TIME (100lu) // We measure in 100 ns units
#define HISTO_BUCKETS (1000000) // Buckets
#define HISTO_HARD_TIMEOUT (HISTO_BUCKETS)

#define HISTO_TIME_TO_SEC(t) ((t) / (HISTO_TIMEUNIT_MULT/HISTO_TIME))
#define PRINT_THRES (1)
#define HISTO_BUCKET_VAL(ptr) ((*(ptr)))
#define HISTO_BUCKET_INC(ptr) ((*(ptr))++)

typedef uint64_t histo_count_t;
struct histogram {
	histo_count_t *arr;
	histo_count_t above_threshold;
};


static inline uint64_t get_time() {
	double t = rte_get_tsc_cycles();
	return (uint64_t)(t * (HISTO_TIMEUNIT_MULT / HISTO_TIME) 
				/ rte_get_tsc_hz());
}

static inline const char* choose_unit_str(int TIMEUNIT)
{
	if(TIMEUNIT <= 1)
		return "sec";
	if(TIMEUNIT <= 1000)
		return "msec";
	if(TIMEUNIT <= 1000*1000)
		return "usec";
	//if(TIMEUNIT <= 1000*1000*1000)
	return "nsec";
}

static inline int choose_unit_mult(int TIMEUNIT)
{
	if(TIMEUNIT <= 1)
		return 1;
	if(TIMEUNIT <= 1000)
		return TIMEUNIT/1000;
	if(TIMEUNIT <= 1000*1000)
		return TIMEUNIT/(1000*1000);
	//if(TIMEUNIT <= 1000*1000*1000)
	return TIMEUNIT/(1000*1000*1000);
}

static void print_hist(struct histogram* hist) {
	const int timeunit_mult = choose_unit_mult(HISTO_TIMEUNIT_MULT);
	const char* timeunit_name = choose_unit_str(HISTO_TIMEUNIT_MULT);
	printf("Unit: %lu %s\n", HISTO_TIME * timeunit_mult, timeunit_name);
	histo_count_t *arr = hist->arr;
	for(int i=0; i<HISTO_BUCKETS; i++) {
		size_t current_size = HISTO_BUCKET_VAL(arr+i);
		if (current_size > 0)
			printf(" <  %9lu %s: %16lu pkts  \n",
				(i+1)*HISTO_TIME*timeunit_mult, 
				timeunit_name, current_size);
	}
	printf("above threshold %09lu pkts \n", hist->above_threshold);
}

// Add histogram b's observations into a, so that a contains all.
static struct histogram* combine_histograms(struct histogram* a, 
					struct histogram* b) {
	for (int i=0; i<HISTO_BUCKETS; i++) {
		*(a->arr + i) += (*(b->arr + i));
	}
	a->above_threshold += b->above_threshold;
	return a;
}

static void print_summary(struct histogram* hist) {
	// NOTE: This is destructive.
	uint64_t total = 0;
	uint64_t count = 0;
	uint64_t min = 0;
	uint64_t max = 0;
	int max_bucket=0;
	int found_min = 0;
	const int timeunit_mult = choose_unit_mult(HISTO_TIMEUNIT_MULT);
	const char* timeunit_name = choose_unit_str(HISTO_TIMEUNIT_MULT);
	printf("   Unit: %lu %s\n", HISTO_TIME * timeunit_mult, timeunit_name);
	printf("\n\nSummary Statistics\n");
	printf("-----------------------------------------------------------\n");
	histo_count_t *arr = hist->arr;
	for(int i=0; i<HISTO_BUCKETS; i++) {
		size_t current_size = HISTO_BUCKET_VAL(arr+i);
		uint64_t latency = (i+1)*HISTO_TIME*timeunit_mult;
		if (!found_min && current_size > 0) {
			min = latency;
			found_min = 1;
		}
		if (current_size > 0) {
			max = latency;
			max_bucket=i;
		}
		histo_count_t* bucket = arr + i;
		uint64_t samples = HISTO_BUCKET_VAL(bucket);
		count += samples;
		*bucket = count;
		total += (samples * latency);
	}

	if (count == 0) {
		printf("No data recorded\n");
		return;
	}

	uint64_t counts[] =	{count / 100, 
				 count / 2, 
				(count * 99) / 100,
				(count * 999) / 1000,
				(uint64_t)((double)count * (0.9999)),
				(uint64_t)((double)count * (0.99999)),
				(uint64_t)((double)count * (0.999999))};
	
	uint64_t latencies[] = {0,0,0,0,0,0,0};

	for (int i=0; i<max_bucket; i++) {
		uint64_t latency = (i+1)*HISTO_TIME*timeunit_mult;
		for (int j=0; j<sizeof(counts)/sizeof(uint64_t); j++) {
			if(HISTO_BUCKET_VAL(arr + i) < counts[j]) {
				latencies[j] = latency;
			} 
		}
	}

	printf("##   Min: %lu %s\n", min, timeunit_name);
	printf("##   Avg: %lu %s\n", (total/count), timeunit_name);
	printf("##   Max: %lu %s\n", max, timeunit_name);
	printf("##   1%%ile: %lu %s\n", latencies[0], timeunit_name);
	printf("##   50%%ile: %lu %s\n", latencies[1], timeunit_name);
	printf("##   99%%ile: %lu %s\n", latencies[2], timeunit_name);
	printf("##   99.9%%ile: %lu %s\n", latencies[3], timeunit_name);
	printf("##   99.99%%ile: %lu %s\n", latencies[4], timeunit_name);
	printf("##   99.999%%ile: %lu %s\n", latencies[5], timeunit_name);
	printf("##   99.9999%%ile: %lu %s\n", latencies[6], timeunit_name);
	printf("##   Total: %lu\n", count);
}

static inline void record_latency(struct histogram* hist, uint64_t latency) {
	if (latency >= HISTO_HARD_TIMEOUT) {
		hist->above_threshold++;
	} else {
		histo_count_t* bucket = hist->arr + latency;
		HISTO_BUCKET_INC(bucket);
	}
}

static inline void init_hist(struct histogram* hist) {
	hist->arr = mem_alloc(HISTO_BUCKETS * sizeof(histo_count_t));
}

#endif
