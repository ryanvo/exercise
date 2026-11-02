// Minimal header-only port of the HdrHistogram (Gil Tene) bucketing scheme.
// Public domain / CC0 for this exercise.
//
// Layout: values are split into log-sized buckets, each holding a linear run
// of sub-buckets. The number of sub-buckets is chosen so that every value is
// stored with the requested number of significant decimal digits.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hdr {

class Histogram {
 public:
  explicit Histogram(int64_t highest_trackable_value = 10'000'000'000LL,
                     int sigfigs = 3) {
    sigfigs = std::min(std::max(sigfigs, 0), 5);
    highest_ = std::max<int64_t>(highest_trackable_value, 2);
    const double largest_single_unit = 2.0 * std::pow(10.0, sigfigs);
    const int magnitude =
        static_cast<int>(std::ceil(std::log2(largest_single_unit)));
    sub_half_mag_ = std::max(magnitude - 1, 0);
    sub_count_ = int64_t{1} << (sub_half_mag_ + 1);
    sub_half_count_ = sub_count_ / 2;
    sub_mask_ = sub_count_ - 1;
    int buckets = 1;  // one bucket per doubling above the sub-bucket span
    for (int64_t untrackable = sub_count_; untrackable <= highest_;
         untrackable <<= 1) {
      ++buckets;
    }
    counts_.assign(static_cast<size_t>((buckets + 1) * sub_half_count_), 0);
    reset();
  }

  void record(int64_t v) { record_n(v, 1); }

  void record_n(int64_t v, int64_t count) {
    if (count <= 0) return;
    v = std::min(std::max<int64_t>(v, 0), highest_);
    counts_[counts_index(v)] += count;
    total_ += count;
    min_ = std::min(min_, v);
    max_ = std::max(max_, v);
  }

  int64_t count() const { return total_; }
  int64_t min() const { return total_ == 0 ? 0 : min_; }
  int64_t max() const { return total_ == 0 ? 0 : max_; }

  double mean() const {
    if (total_ == 0) return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < counts_.size(); ++i) {
      if (counts_[i] == 0) continue;
      const int64_t v = value_from_index(i);
      sum += static_cast<double>(counts_[i]) *
             static_cast<double>(median_equivalent(v));
    }
    return sum / static_cast<double>(total_);
  }

  // p in [0, 100]. Returns the highest value equivalent to the sampled
  // bucket, in the same way as HdrHistogram's valueAtPercentile.
  int64_t value_at_percentile(double p) const {
    if (total_ == 0) return 0;
    p = std::min(std::max(p, 0.0), 100.0);
    const double wanted = (p / 100.0) * static_cast<double>(total_) + 0.5;
    const int64_t target = std::max<int64_t>(static_cast<int64_t>(wanted), 1);
    int64_t seen = 0;
    for (size_t i = 0; i < counts_.size(); ++i) {
      seen += counts_[i];
      if (seen >= target) return highest_equivalent(value_from_index(i));
    }
    return max();
  }

  void merge(const Histogram& other) {
    if (other.total_ == 0) return;
    for (size_t i = 0; i < other.counts_.size(); ++i) {
      if (other.counts_[i] != 0) {
        record_n(other.value_from_index(i), other.counts_[i]);
      }
    }
    // Raw extremes survive the re-bucketing when both layouts agree.
    min_ = std::min(min_, other.min_);
    max_ = std::max(max_, other.max_);
  }

  void reset() {
    std::fill(counts_.begin(), counts_.end(), 0);
    total_ = 0;
    min_ = highest_;
    max_ = 0;
  }

  int64_t highest_trackable_value() const { return highest_; }
  int64_t lowest_equivalent(int64_t v) const {
    const int b = bucket_index(v);
    return sub_bucket_index(v, b) << b;
  }
  int64_t highest_equivalent(int64_t v) const {
    return lowest_equivalent(v) + size_of_equivalent_range(v) - 1;
  }
  int64_t median_equivalent(int64_t v) const {
    return lowest_equivalent(v) + (size_of_equivalent_range(v) >> 1);
  }

 private:
  static int bit_width(int64_t v) {
    int n = 0;
    while (v > 0) {
      ++n;
      v >>= 1;
    }
    return n;
  }

  int bucket_index(int64_t v) const {
    return bit_width(v | sub_mask_) - (sub_half_mag_ + 1);
  }
  int64_t sub_bucket_index(int64_t v, int bucket) const {
    return v >> bucket;
  }
  size_t counts_index(int64_t v) const {
    const int b = bucket_index(v);
    const int64_t base = int64_t{b + 1} << sub_half_mag_;
    return static_cast<size_t>(base + sub_bucket_index(v, b) - sub_half_count_);
  }
  int64_t value_from_index(size_t idx) const {
    int b = static_cast<int>(idx >> sub_half_mag_) - 1;
    int64_t sub = static_cast<int64_t>(idx & static_cast<size_t>(sub_half_count_ - 1)) +
                  sub_half_count_;
    if (b < 0) {
      sub -= sub_half_count_;
      b = 0;
    }
    return sub << b;
  }
  int64_t size_of_equivalent_range(int64_t v) const {
    const int b = bucket_index(v);
    const int adjusted = sub_bucket_index(v, b) >= sub_count_ ? b + 1 : b;
    return int64_t{1} << adjusted;
  }

  int64_t highest_ = 0;
  int sub_half_mag_ = 0;
  int64_t sub_count_ = 0;
  int64_t sub_half_count_ = 0;
  int64_t sub_mask_ = 0;
  std::vector<int64_t> counts_;
  int64_t total_ = 0;
  int64_t min_ = 0;
  int64_t max_ = 0;
};

}  // namespace hdr
