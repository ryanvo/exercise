#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "arbitrator.h"
#include "queue.h"

struct HandlerConfig {
  uint16_t port_a = 14000;
  uint16_t port_b = 14001;
  uint64_t gap_timeout_ns = 500000;
  int workers = 1;  // consumer threads
  int batch = 1;    // messages taken from the queue per wakeup
  uint64_t idle_limit_ns = 30000000000ULL;  // give up if no end of session arrives
};

struct Level {
  uint64_t count = 0;
  uint64_t volume = 0;
};

struct Snapshot {
  uint64_t consumed = 0;
  uint64_t rejected = 0;
  std::size_t levels = 0;
  std::size_t max_depth = 0;
};

class Handler {
 public:
  using Callback = std::function<void(const Message&)>;

  Handler(HandlerConfig cfg, Callback cb);
  ~Handler();
  void start();
  void stop();
  void join();
  bool done() const { return done_.load(std::memory_order_acquire); }
  Snapshot snapshot();
  const ArbStats& arb_stats() const { return arb_.stats(); }
  const std::vector<std::string>& arb_events() const { return arb_.events(); }

 private:
  struct QueueSink {
    MessageQueue& q;
    void operator()(const Message& m) {
      std::unique_ptr<Envelope> e(new Envelope);
      e->msg = m;
      q.push(std::move(e));
    }
  };
  void receive_loop();
  void consume_loop();

  HandlerConfig cfg_;
  Callback cb_;
  MessageQueue queue_;
  QueueSink sink_{queue_};
  Arbitrator<QueueSink> arb_;
  std::unordered_map<std::string, Level> levels_;
  uint64_t consumed_ = 0;
  uint64_t rejected_ = 0;
  int fds_[2] = {-1, -1};
  std::thread receiver_;
  std::vector<std::thread> workers_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> done_{false};
};
