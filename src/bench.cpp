// Measures the time from socket receive to consumer callback for every message,
// with the simulator sending at a constant rate from an in-process thread.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "handler.h"
#include "hdr_histogram.h"
#include "sim.h"

struct RunResult {
  hdr::Histogram recv_to_consumer, send_to_consumer;
  ArbStats arb;
  Snapshot snap;
  double throughput = 0;  // msgs/s, unthrottled phase
  uint64_t sender_lag_ns = 0;
};

static double us(int64_t ns) { return static_cast<double>(ns) / 1000.0; }

static void print_hist(const char* name, const hdr::Histogram& h) {
  std::printf("  %-16s min=%.2fus p50=%.2fus p99=%.2fus p99.9=%.2fus max=%.2fus (n=%lld)\n", name, us(h.min()),
              us(h.value_at_percentile(50)), us(h.value_at_percentile(99)), us(h.value_at_percentile(99.9)), us(h.max()),
              (long long)h.count());
}

static uint64_t clock_overhead_ns() {
  const uint64_t t0 = now_ns();
  uint64_t sink = 0;
  for (int i = 0; i < 1000000; ++i) sink += now_ns() & 1;
  return (now_ns() - t0 + sink / 1000000) / 1000000;
}

// Runs one simulated session through a handler. rate == 0 means unthrottled.
static void run_once(SimConfig sim, const HandlerConfig& hcfg, uint64_t warmup, RunResult& r) {
  Plan plan = plan_schedule(sim);
  uint64_t first_ns = 0, last_ns = 0, seen = 0;
  Handler h(hcfg, [&](const Message& m) {
    const uint64_t t = now_ns();
    if (!first_ns) first_ns = t;
    last_ns = t;
    ++seen;
    if (m.seq <= warmup) return;
    r.recv_to_consumer.record(static_cast<int64_t>(t - m.recv_ns));
    r.send_to_consumer.record(static_cast<int64_t>(t - m.send_ns));
  });
  h.start();
  std::thread sender([&] { r.sender_lag_ns = run_sim(sim, plan).max_lag_ns; });
  uint64_t next_report = now_ns() + 250000000ULL;
  while (!h.done()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (now_ns() >= next_report) {
      const Snapshot s = h.snapshot();
      if (sim.rate > 0) std::printf("    ... consumed=%llu depth_max=%zu\n", (unsigned long long)s.consumed, s.max_depth);
      next_report += 250000000ULL;
    }
  }
  sender.join();
  h.join();
  r.arb = h.arb_stats();
  r.snap = h.snapshot();
  if (last_ns > first_ns) r.throughput = static_cast<double>(seen) * 1e9 / static_cast<double>(last_ns - first_ns);
}

static double cv(const std::vector<double>& v) {
  double m = 0, s = 0;
  for (double x : v) m += x;
  m /= static_cast<double>(v.size());
  for (double x : v) s += (x - m) * (x - m);
  return v.size() > 1 ? std::sqrt(s / static_cast<double>(v.size() - 1)) / m * 100.0 : 0.0;
}

int main(int argc, char** argv) {
  SimConfig sim;
  HandlerConfig hcfg;
  int runs = 1;
  uint64_t warmup = 20000;
  for (int i = 1; i + 1 < argc; i += 2) {
    const char* k = argv[i];
    const char* v = argv[i + 1];
    if (!std::strcmp(k, "--msgs")) sim.msgs = std::strtoull(v, nullptr, 10);
    else if (!std::strcmp(k, "--rate")) sim.rate = std::atof(v);
    else if (!std::strcmp(k, "--seed")) sim.seed = std::strtoull(v, nullptr, 10);
    else if (!std::strcmp(k, "--runs")) runs = std::atoi(v);
    else if (!std::strcmp(k, "--warmup")) warmup = std::strtoull(v, nullptr, 10);
    else if (!std::strcmp(k, "--drop")) sim.drop = std::atof(v);
    else if (!std::strcmp(k, "--dup")) sim.dup = std::atof(v);
    else if (!std::strcmp(k, "--reorder")) sim.reorder = std::atof(v);
    else if (!std::strcmp(k, "--workers")) hcfg.workers = std::atoi(v);
    else if (!std::strcmp(k, "--batch")) hcfg.batch = std::atoi(v);
    else if (!std::strcmp(k, "--gap-timeout-us")) hcfg.gap_timeout_ns = std::strtoull(v, nullptr, 10) * 1000;
    else { std::fprintf(stderr, "unknown option %s\n", k); return 2; }
  }
  sim.schedule_path = "bench-schedule.txt";
#ifdef __APPLE__
  const char* placement = "qos:user-interactive (macOS)";
#else
  const char* placement = "default scheduler";
#endif
  std::printf("bench: clock=steady_clock cost=%lluns/read threads=%s\n", (unsigned long long)clock_overhead_ns(), placement);
  std::printf("bench: seed=%llu msgs=%llu rate=%.0f/s warmup=%llu drop=%.3f dup=%.3f reorder=%.3f workers=%d batch=%d runs=%d\n",
              (unsigned long long)sim.seed, (unsigned long long)sim.msgs, sim.rate, (unsigned long long)warmup, sim.drop, sim.dup,
              sim.reorder, hcfg.workers, hcfg.batch, runs);
  std::vector<double> p50s, p999s, tputs;
  for (int run = 1; run <= runs; ++run) {
    RunResult r;
    SimConfig timed = sim;
    timed.seed = sim.seed + static_cast<uint64_t>(run - 1);
    run_once(timed, hcfg, warmup, r);
    SimConfig burst = timed;
    burst.rate = 0;
    burst.quiet_ns = 5000000;
    burst.max_per_packet = 40;
    RunResult b;
    run_once(burst, hcfg, sim.msgs, b);
    std::printf("run %d: delivered=%llu late=%llu lost=%llu held=%llu dupes=%llu consumed=%llu depth_max=%zu sender_lag_max=%.0fus\n",
                run, (unsigned long long)r.arb.delivered, (unsigned long long)r.arb.late, (unsigned long long)r.arb.lost,
                (unsigned long long)r.arb.held, (unsigned long long)r.arb.dupes, (unsigned long long)r.snap.consumed,
                r.snap.max_depth, us(static_cast<int64_t>(r.sender_lag_ns)));
    print_hist("recv->consumer", r.recv_to_consumer);
    print_hist("send->consumer", r.send_to_consumer);
    std::printf("  %-16s %.2fM msgs/s unthrottled (%llu msgs, depth_max=%zu)\n", "throughput", b.throughput / 1e6,
                (unsigned long long)b.snap.consumed, b.snap.max_depth);
    p50s.push_back(us(r.recv_to_consumer.value_at_percentile(50)));
    p999s.push_back(us(r.recv_to_consumer.value_at_percentile(99.9)));
    tputs.push_back(b.throughput / 1e6);
  }
  if (runs > 1) {
    std::printf("spread over %d runs (recv->consumer):\n  p99.9:", runs);
    for (double x : p999s) std::printf(" %.2fus", x);
    std::printf("  cv=%.1f%%\n  p50:  ", cv(p999s));
    for (double x : p50s) std::printf(" %.2fus", x);
    std::printf("  cv=%.1f%%\n  throughput:", cv(p50s));
    for (double x : tputs) std::printf(" %.2fM/s", x);
    std::printf("  cv=%.1f%%\n", cv(tputs));
    if (cv(p999s) > 50.0)
      std::printf("WARNING: p99.9 spread across runs is above 50%%. Close other apps and rerun before drawing conclusions.\n");
  }
  return 0;
}
