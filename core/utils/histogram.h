// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
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

#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

static const std::vector<double> quartiles = {0.25f, 0.5f, 0.75f, 1.0f};

// A general purpose histogram. A bin b_i is labeled by (i+1) * bucket_width_.
// T must be an arithmetic type
template <typename T>
class Histogram {
 public:
  // Construct a new histogram with "num_buckets" buckets of width
  // "bucket_width".
  Histogram(size_t num_buckets, T bucket_width)
      : num_buckets_(num_buckets),
        bucket_width_(bucket_width),
        threshold_((num_buckets_ + 1) * bucket_width_),
        buckets_(),
        above_threshold_(),
        count_(),
        total_(),
        min_bucket_(),
        max_bucket_() {
    buckets_ = new size_t[num_buckets_];
    reset();
  }

  ~Histogram() { delete[] buckets_; }

  Histogram(const Histogram&) = delete;
  Histogram& operator=(const Histogram&) = delete;

  // Insert x into the histogram.
  void insert(T x) {
    if (count_) {
      reset();
    }

    if (x >= threshold_) {
      above_threshold_++;
      return;
    }
    (*bucket(x))++;
  }

  // Returns the number of inserted values that were >= (num_buckets_ + 1) *
  // bucket_width_
  size_t above_threshold() const { return above_threshold_; }

  // Return the bucket with the lowest frequency.
  // NOTE: This function s destructive, i.e. it turns the histogram into a
  // cummulative histogram. Any calls to insert() after calls to this function
  // will result in a call to reset().
  T min() {
    if (!count_) {
      summarize();
    }
    return (min_bucket_ + 1) * bucket_width_;
  }

  // Return the bucket with the highest frequency.
  // NOTE: This function s destructive, i.e. it turns the histogram into a
  // cummulative histogram. Any calls to insert() after calls to this function
  // will result in a call to reset().
  T max() {
    if (!count_) {
      summarize();
    }
    return (max_bucket_ + 1) * bucket_width_;
  }

  // Return the average value in the histogram.
  // NOTE: This function s destructive, i.e. it turns the histogram into a
  // cummulative histogram. Any calls to insert() after calls to this function
  // will result in a call to reset().
  T avg() {
    if (!count_) {
      summarize();
    }
    if (count_) {
      return total_ / count_;
    }
    return 0;
  }

  // Return the sum of the frequencies of each bucket.
  // NOTE: This function s destructive, i.e. it turns the histogram into a
  // cummulative histogram. Any calls to insert() after calls to this function
  // will result in a call to reset().
  size_t count() {
    if (!count_) {
      summarize();
    }
    return count_;
  }

  // Return the total of values inserted into the histogram, i.e. the sum of
  // (i+1) * bucket_width_ * count(b_i) for each bucket b_i
  // NOTE: This function s destructive, i.e. it turns the histogram into a
  // cummulative histogram. Any calls to insert() after calls to this function
  // will result in a call to reset().
  T total() {
    if (!count_) {
      summarize();
    }
    return total_;
  }

  // Return the largest bucket b_i for which the cummulative frequency is less
  // than p % of the total. p should be in [0, 100].
  // NOTE: This function s destructive, i.e. it turns the histogram into a
  // cummulative histogram. Any calls to insert() after calls to this function
  // will result in a call to reset().
  T percentile(double p) {
    p /= 100.0;
    if (!count_) {
      summarize();
    }

    T ret = 0;
    size_t p_count = (size_t)(p * count_);
    for (size_t i = 0; i < max_bucket_; i++) {
      if (buckets_[i] < p_count) {
        ret = i + 1;
      }
    }
    return ret * bucket_width_;
  }

  // Zero out the histogram.
  void reset() {
    count_ = 0;
    total_ = 0;
    min_bucket_ = 0;
    max_bucket_ = 0;
    above_threshold_ = 0;
    memset(buckets_, 0, num_buckets_ * sizeof(size_t));
  }

 private:
  // TODO(melvin): add support for logarithmic binning
  size_t *bucket(T x) { return buckets_ + (uintptr_t)(x / bucket_width_); }

  // Convert the histogram into a cummulative histogram. Called by min(), max(),
  // avg(), count(), total() and percentile().
  void summarize() {
    bool found_min = false;
    count_ = 0;
    total_ = 0;
    max_bucket_ = 0;
    for (size_t i = 0; i < num_buckets_; i++) {
      size_t samples = buckets_[i];
      if (!found_min && samples > 0) {
        min_bucket_ = i;
        found_min = true;
      }
      if (samples > 0) {
        max_bucket_ = i;
      }
      count_ += samples;
      buckets_[i] = count_;
      total_ += (samples * (i + 1) * bucket_width_);
    }
  }

  size_t num_buckets_;
  T bucket_width_;
  T threshold_;
  size_t *buckets_;
  size_t above_threshold_;
  size_t count_;
  T total_;
  size_t min_bucket_;
  size_t max_bucket_;
};

#endif  // BESS_UTILS_HISTOGRAM_H_
