// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2019, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_UTILS_HISTOGRAM_H_
#define BESS_UTILS_HISTOGRAM_H_

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

#include <glog/logging.h>

#include "../pb/util_msg.pb.h"

// Class for general purpose histogram. T generally should be an
// integral type, though floating point types will also work.
// A bin b_i corresponds for the range [i * width, (i + 1) * width)
// (note that it's left-closed and right-open), and (i * width) is used for its
// representative value.
template <typename T = uint64_t>
class Histogram {
 public:
  static_assert(std::is_arithmetic<T>::value, "Arithmetic type required.");
  struct Summary {
    size_t num_buckets;  // Number of buckets in the histogram
    size_t bucket_width; // Resolution of the measured data
    size_t count;        // # of samples (including above_range). If 0, min, max and avg are also 0
    size_t above_range;  // # of samples beyond the histogram range
    T min;               // Min value
    T max;               // Max value. May be underestimated if above_range > 0
    T avg;               // Average of all samples (== total / count)
    T total;             // Total sum of all samples
    std::vector<T> percentile_values;
  };

  // Construct a new histogram with "num_buckets" buckets of width
  // "bucket_width".
  Histogram(size_t num_buckets, T bucket_width)
      : bucket_width_(bucket_width), buckets_(num_buckets + 1) {}

  // Swap operator allows clean summarizing with external lock,
  // when using a histogram on an active data stream.
  // Create a new, empty histogram of the appropriate size
  // while NOT holding a lock, then use tmphist.swap(datahist)
  // while holding a lock.  Release the lock, and summarize
  // from tmphist while new data accumulate into datahist.
  void swap(Histogram &other) noexcept {
    using std::swap;
    swap(bucket_width_, other.bucket_width_);
    swap(buckets_, other.buckets_);
  }

  // Move constructor and assignment operator to take from another
  // histogram -- note that this is very much non-atomic.
  Histogram(Histogram &&other) noexcept {
    bucket_width_ = other.bucket_width_;
    buckets_ = std::move(other.buckets_);
  }

  Histogram &operator=(Histogram &&other) noexcept {
    bucket_width_ = other.bucket_width_;
    buckets_ = std::move(other.buckets_);
    return *this;
  }

  // Inserts x into the histogram.
  // Note: this particular insert is NOT atomic.
  void Insert(T x) {
    // The last element of the buckets_ is used to count data points above
    // the upper bound of the histogram range.
    size_t index = x / bucket_width_;
    index = std::min(index, buckets_.size() - 1);
    buckets_[index].store(1 + buckets_[index].load(std::memory_order_relaxed),
                          std::memory_order_relaxed);
  }

  // Inserts x into the histogram.
  // Note: this particular insert IS atomic.
  void AtomicInsert(T x) {
    // The last element of the buckets_ is used to count data points above
    // the upper bound of the histogram range.
    size_t index = x / bucket_width_;
    index = std::min(index, buckets_.size() - 1);
    buckets_[index].fetch_add(1);
  }

  // Returns the summary of the histogram.
  // "percentiles" is a vector of doubles, whose values are in the range of
  // [0.0, 100.0] and monotonically increasing. E.g., {50.0, 90.0, 99.0, 99.9}
  // X'th percentile of a histogram is defined as the smallest
  // representative value of a non-empty bucket i, which satisfies that
  // sum(bucket 0..i) / sum(bucket 0..N) * 100.0 >= X
  // This approximation should work well if: (1) bucket width is small and
  // (2) there are enough data points.
  // Each of percentiles is calculated and its value is returned in
  // percentile_values
  const Summary Summarize(const std::vector<double> &percentiles = {}) const {
    Summary ret = {};
    ret.num_buckets = num_buckets();
    ret.bucket_width = bucket_width_;
    uint64_t count = std::accumulate(buckets_.begin(), buckets_.end(), 0);
    ret.count = count;
    ret.above_range = buckets_.back();
    ret.percentile_values = std::vector<T>(percentiles.size());

    bool found_min = false;
    size_t count_so_far = 0;
    T total = 0;
    auto percentile_it = percentiles.cbegin();
    auto percentile_value_it = ret.percentile_values.begin();

    for (size_t i = 0; i < buckets_.size(); i++) {
      T val = i * bucket_width_;
      T freq = buckets_[i];
      total += val * freq;
      count_so_far += freq;

      if (freq > 0) {
        if (!found_min) {
          ret.min = val;
          found_min = true;
        }
        ret.max = val;

        while (percentile_it != percentiles.end()) {
          DCHECK_LE(0.0, *percentile_it);
          DCHECK_LE(*percentile_it, 100.0);

          // Perform integer comparison first for the special case 100'th %-ile
          if (count_so_far < count &&
              (count_so_far * 100.0) / count - *percentile_it <
                  std::numeric_limits<double>::epsilon()) {
            break;
          }

          *percentile_value_it = val;
          percentile_value_it++;
          percentile_it++;

          // should be monotonic
          DCHECK(percentile_it == percentiles.end() ||
                 *(percentile_it - 1) < *percentile_it);
        }
      }
    }

    ret.avg = (count > 0) ? total / count : 0;
    ret.total = total;
    return ret;
  }

  size_t num_buckets() const { return buckets_.size() - 1; }
  T bucket_width() const { return bucket_width_; }

  size_t max_num_buckets() const {
    // This constant is mainly to keep resets to a reasonable speed.
    const size_t max_buckets = 10'000'000;
    return std::min(max_buckets, buckets_.max_size());
  }

  // Resets all counters, and the count of such counters.
  // Note that the number of buckets remains unchanged.
  void Reset() { std::fill(buckets_.begin(), buckets_.end(), T()); }

  // Resize the histogram.  Note that this resets it (i.e., this
  // does not attempt to redistribute existing counts).
  void Resize(size_t num_buckets, T bucket_width) {
    buckets_ = std::move(std::vector<std::atomic<uint64_t>>(num_buckets + 1));
    bucket_width_ = bucket_width;
  }

 private:
  // TODO(melvin): add support for logarithmic binning
  T bucket_width_;
  std::vector<std::atomic<uint64_t>> buckets_;
};

bool IsValidPercentiles(const std::vector<double> &percentiles);

template <typename T>
void SetSummary(bess::pb::HistogramSummary *r, const T &summary) {
  r->set_num_buckets(summary.num_buckets);
  r->set_bucket_width(summary.bucket_width);
  r->set_count(summary.count);
  r->set_above_range(summary.above_range);
  r->set_min(summary.min);
  r->set_max(summary.max);
  r->set_avg(summary.avg);
  r->set_total(summary.total);
  for (const auto &val : summary.percentile_values) {
    r->add_percentile_values(val);
  }
}

#endif  // BESS_UTILS_HISTOGRAM_H_
