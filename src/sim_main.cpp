#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sim.h"

int main(int argc, char** argv) {
  SimConfig cfg;
  for (int i = 1; i + 1 < argc; i += 2) {
    const char* k = argv[i];
    const char* v = argv[i + 1];
    if (!std::strcmp(k, "--msgs")) cfg.msgs = std::strtoull(v, nullptr, 10);
    else if (!std::strcmp(k, "--rate")) cfg.rate = std::atof(v);
    else if (!std::strcmp(k, "--seed")) cfg.seed = std::strtoull(v, nullptr, 10);
    else if (!std::strcmp(k, "--drop")) cfg.drop = std::atof(v);
    else if (!std::strcmp(k, "--dup")) cfg.dup = std::atof(v);
    else if (!std::strcmp(k, "--reorder")) cfg.reorder = std::atof(v);
    else if (!std::strcmp(k, "--reorder-delay-us")) cfg.reorder_delay_ns = std::strtoull(v, nullptr, 10) * 1000;
    else if (!std::strcmp(k, "--skew-b-us")) cfg.skew_b_ns = std::strtoull(v, nullptr, 10) * 1000;
    else if (!std::strcmp(k, "--schedule")) cfg.schedule_path = v;
    else { std::fprintf(stderr, "unknown option %s\n", k); return 2; }
  }
  Plan plan = plan_schedule(cfg);
  write_schedule(plan, cfg);
  const SimResult r = run_sim(cfg, plan);
  std::printf("sim: seed=%llu msgs=%llu packets_sent=%llu reachable_seqs=%llu sender_max_lag=%lluus schedule=%s\n",
              (unsigned long long)cfg.seed, (unsigned long long)cfg.msgs, (unsigned long long)r.packets_sent,
              (unsigned long long)plan.reachable, (unsigned long long)(r.max_lag_ns / 1000), cfg.schedule_path.c_str());
  return 0;
}
