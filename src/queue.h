#pragma once
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

#include "message.h"

// One queue slot: the message plus decode scratch space for the consumer.
struct Envelope {
  Message msg;
  uint8_t scratch[256 * 1024];
};

// Receiver -> consumer handoff.
class MessageQueue {
 public:
  void push(std::unique_ptr<Envelope> m) {
    {
      std::lock_guard<std::mutex> g(mu_);
      q_.push_back(std::move(m));
      if (q_.size() > max_depth_) max_depth_ = q_.size();
    }
    cv_.notify_one();
  }

  // Waits for at least one message, then hands up to max_n messages to fn while the
  // queue is held. Returns the number handed over; 0 only after close().
  template <class Fn>
  std::size_t drain(std::size_t max_n, Fn&& fn) {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&] { return !q_.empty() || closed_; });
    std::size_t n = 0;
    while (!q_.empty() && n < max_n) {
      std::unique_ptr<Envelope> m = std::move(q_.front());
      q_.pop_front();
      fn(m->msg);
      ++n;
    }
    return n;
  }

  void close() {
    { std::lock_guard<std::mutex> g(mu_); closed_ = true; }
    cv_.notify_all();
  }
  bool closed() const { std::lock_guard<std::mutex> g(mu_); return closed_ && q_.empty(); }
  std::size_t max_depth() const { std::lock_guard<std::mutex> g(mu_); return max_depth_; }
  std::mutex& mutex() { return mu_; }

 private:
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::unique_ptr<Envelope>> q_;
  std::size_t max_depth_ = 0;
  bool closed_ = false;
};
