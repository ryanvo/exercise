#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "message.h"

struct SimConfig {
  uint64_t msgs = 200000;
  double rate = 50000;  // messages per second; 0 = unthrottled
  uint64_t seed = 1;
  double drop = 0.01;     // per packet, per feed
  double dup = 0.01;      // per packet, per feed: packet sent twice
  double reorder = 0.02;  // per packet, per feed: packet delayed by reorder_delay_ns
  uint64_t reorder_delay_ns = 1500000;
  uint64_t skew_b_ns = 100000;  // feed B lags feed A
  int max_per_packet = 3;
  uint64_t quiet_ns = 25000000;  // pause before end-of-session packets
  uint16_t port_a = 14000;
  uint16_t port_b = 14001;
  std::string schedule_path = "schedule.txt";
};

enum class Act : uint8_t { Send, Drop, Dup, Delay, Heartbeat, End };

struct Event {
  uint64_t t_ns;         // when the packet leaves (relative to start)
  uint64_t intended_ns;  // when it was meant to leave (before any delay)
  uint64_t seq;          // first sequence number in the packet
  uint16_t count;
  uint8_t feed;
  Act act;
};

struct Plan {
  std::vector<Event> events;  // sorted by t_ns
  std::vector<WireMsg> msgs;  // index = seq - 1
  uint64_t reachable = 0;     // sequence numbers that leave on at least one feed
};

Plan plan_schedule(const SimConfig& cfg);
void write_schedule(const Plan& plan, const SimConfig& cfg);
struct SimResult {
  uint64_t packets_sent = 0;
  uint64_t max_lag_ns = 0;  // largest distance behind the schedule the sender fell
};
// Sends the plan over UDP to 127.0.0.1:port_a / port_b. Blocks until the last packet leaves.
SimResult run_sim(const SimConfig& cfg, const Plan& plan);
