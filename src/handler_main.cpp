#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "handler.h"

int main(int argc, char** argv) {
  HandlerConfig cfg;
  bool events = false;
  for (int i = 1; i < argc; ++i) {
    const char* k = argv[i];
    if (!std::strcmp(k, "--events")) { events = true; continue; }
    if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", k); return 2; }
    const char* v = argv[++i];
    if (!std::strcmp(k, "--gap-timeout-us")) cfg.gap_timeout_ns = std::strtoull(v, nullptr, 10) * 1000;
    else if (!std::strcmp(k, "--workers")) cfg.workers = std::atoi(v);
    else if (!std::strcmp(k, "--batch")) cfg.batch = std::atoi(v);
    else { std::fprintf(stderr, "unknown option %s\n", k); return 2; }
  }
  uint64_t max_seq = 0, out_of_order = 0;
  Handler h(cfg, [&](const Message& m) {
    if (m.seq < max_seq && !m.late) ++out_of_order;
    if (m.seq > max_seq) max_seq = m.seq;
  });
  h.start();
  std::printf("handler: listening on 127.0.0.1:%u (A) and :%u (B), gap timeout %llu us\n", cfg.port_a, cfg.port_b,
              (unsigned long long)(cfg.gap_timeout_ns / 1000));
  std::fflush(stdout);
  while (!h.done()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  h.join();
  const ArbStats& s = h.arb_stats();
  const Snapshot snap = h.snapshot();
  std::printf("handler: delivered=%llu in_order=%llu late=%llu lost=%llu gaps=%llu held=%llu dupes=%llu line_dupes=%llu/%llu "
              "consumed=%llu max_seq=%llu out_of_order=%llu\n",
              (unsigned long long)s.delivered, (unsigned long long)(s.delivered - s.late), (unsigned long long)s.late,
              (unsigned long long)s.lost, (unsigned long long)s.gaps, (unsigned long long)s.held, (unsigned long long)s.dupes,
              (unsigned long long)s.line_dupes[0], (unsigned long long)s.line_dupes[1], (unsigned long long)snap.consumed,
              (unsigned long long)max_seq, (unsigned long long)out_of_order);
  if (events)
    for (const std::string& e : h.arb_events()) std::printf("  %s\n", e.c_str());
  return 0;
}
