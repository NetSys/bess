#ifndef BESS_UTILS_HISTOGRAM_H_
#define BESS_UTILS_HISTOGRAM_H_

#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

static const std::vector<double> quartiles = {0.25f, 0.5f, 0.75f, 1.0f};

// A general purpose histogram. A bin b_i is labeled by its upper edge
// T must be an arithmetic type
template <typename T>
class Histogram {
 public:
  Histogram(size_t num_buckets, T threshold, T bucket_width)
      : num_buckets_(num_buckets),
        bucket_width_(bucket_width),
        threshold_(threshold),
        buckets_(),
        above_threshold_(),
        count_(),
        total_(),
        min_bucket_(),
        max_bucket_() {
    buckets_ = static_cast<size_t *>(malloc(num_buckets_ * sizeof(size_t)));
    reset();
  }

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

  size_t above_threshold() { return above_threshold_; }

  T min() {
    if (!count_) {
      summarize();
    }
    return (min_bucket_ + 1) * bucket_width_;
  }

  T max() {
    if (!count_) {
      summarize();
    }
    return (max_bucket_ + 1) * bucket_width_;
  }

  T avg() {
    if (!count_) {
      summarize();
    }
    if (count_) {
      return total_ / count_;
    }
    return 0;
  }

  size_t count() {
    if (!count_) {
      summarize();
    }
    return count_;
  }

  T total() {
    if (!count_) {
      summarize();
    }
    return total_;
  }

  T percentile(double p) {
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

  void summarize();

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

template <typename T>
void Histogram<T>::summarize() {
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

#endif  // BESS_UTILS_HISTOGRAM_H_
