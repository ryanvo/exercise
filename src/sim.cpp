#include "sim.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <random>

#include "mold.h"

static const char kSession[mold::kSessionLen + 1] = "FEEDEX0001";

Plan plan_schedule(const SimConfig& cfg) {
  Plan plan;
  std::mt19937_64 rng(cfg.seed);
  std::uniform_real_distribution<double> coin(0.0, 1.0);
  std::uniform_int_distribution<int> pkt_size(1, cfg.max_per_packet);
  std::uniform_int_distribution<int> sym(0, kSymbolCount - 1);
  std::uniform_int_distribution<uint32_t> qty(1, 500);
  const double interval = cfg.rate > 0 ? 1e9 / cfg.rate : 0.0;

  plan.msgs.resize(cfg.msgs);
  for (uint64_t i = 0; i < cfg.msgs; ++i) {
    WireMsg& m = plan.msgs[i];
    m.send_ns = 0;
    m.price = 100000 + static_cast<uint32_t>(rng() % 50000);
    m.qty = qty(rng);
    std::memset(m.sym, 0, sizeof m.sym);
    const char* name = kSymbols[sym(rng)];
    std::memcpy(m.sym, name, std::strlen(name));
  }

  std::vector<char> reached(cfg.msgs, 0);
  uint64_t last_t = 0;
  for (uint64_t seq = 1; seq <= cfg.msgs;) {
    const uint16_t count = static_cast<uint16_t>(std::min<uint64_t>(pkt_size(rng), cfg.msgs - seq + 1));
    const uint64_t t = static_cast<uint64_t>(static_cast<double>(seq - 1) * interval);
    for (uint8_t feed = 0; feed < 2; ++feed) {
      const uint64_t base = t + (feed == 1 ? cfg.skew_b_ns : 0);
      if (coin(rng) < cfg.drop) {
        plan.events.push_back({base, t, seq, count, feed, Act::Drop});
        continue;
      }
      const bool delayed = coin(rng) < cfg.reorder;
      const uint64_t leave = base + (delayed ? cfg.reorder_delay_ns : 0);
      plan.events.push_back({leave, t, seq, count, feed, delayed ? Act::Delay : Act::Send});
      if (coin(rng) < cfg.dup) plan.events.push_back({leave + 2000, t, seq, count, feed, Act::Dup});
      for (uint16_t i = 0; i < count; ++i) reached[seq - 1 + i] = 1;
      last_t = std::max(last_t, leave);
    }
    seq += count;
  }
  for (uint64_t hb = 1000000000ULL; hb < last_t; hb += 1000000000ULL) {
    const uint64_t next = static_cast<uint64_t>(static_cast<double>(hb) / interval) + 1;
    for (uint8_t feed = 0; feed < 2; ++feed) plan.events.push_back({hb, hb, std::min(next, cfg.msgs + 1), 0, feed, Act::Heartbeat});
  }
  for (int k = 0; k < 3; ++k) {
    const uint64_t t = last_t + cfg.quiet_ns + static_cast<uint64_t>(k) * 5000000;
    for (uint8_t feed = 0; feed < 2; ++feed) plan.events.push_back({t, t, cfg.msgs + 1, mold::kEndOfSession, feed, Act::End});
  }
  std::stable_sort(plan.events.begin(), plan.events.end(),
                   [](const Event& a, const Event& b) { return a.t_ns < b.t_ns; });
  for (char r : reached) plan.reachable += r;
  return plan;
}

void write_schedule(const Plan& plan, const SimConfig& cfg) {
  FILE* f = std::fopen(cfg.schedule_path.c_str(), "w");
  if (!f) { std::perror("schedule"); return; }
  std::fprintf(f, "# feed-exercise schedule seed=%llu msgs=%llu rate=%.0f drop=%.4f dup=%.4f reorder=%.4f reorder_delay_us=%llu skew_b_us=%llu\n",
               (unsigned long long)cfg.seed, (unsigned long long)cfg.msgs, cfg.rate, cfg.drop, cfg.dup, cfg.reorder,
               (unsigned long long)(cfg.reorder_delay_ns / 1000), (unsigned long long)(cfg.skew_b_ns / 1000));
  std::fprintf(f, "# columns: leave_us intended_us feed first_seq count action\n");
  static const char* const names[] = {"send", "drop", "dup", "delay", "heartbeat", "end"};
  for (const Event& e : plan.events)
    std::fprintf(f, "%llu %llu %c %llu %u %s\n", (unsigned long long)(e.t_ns / 1000), (unsigned long long)(e.intended_ns / 1000),
                 e.feed ? 'B' : 'A', (unsigned long long)e.seq, e.count, names[static_cast<int>(e.act)]);
  std::fprintf(f, "# reachable_seqs=%llu\n", (unsigned long long)plan.reachable);
  std::fclose(f);
}

static int open_sender(uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (fd < 0 || ::connect(fd, reinterpret_cast<sockaddr*>(&to), sizeof to) != 0) { std::perror("sim socket"); return -1; }
  return fd;
}

SimResult run_sim(const SimConfig& cfg, const Plan& plan) {
  prefer_performance_core();
  const int fds[2] = {open_sender(cfg.port_a), open_sender(cfg.port_b)};
  uint8_t pkt[1500];
  SimResult r;
  const uint64_t t0 = now_ns();
  for (const Event& e : plan.events) {
    if (e.act == Act::Drop) continue;
    uint64_t t = now_ns();
    while (t < t0 + e.t_ns) t = now_ns();
    if (t - (t0 + e.t_ns) > r.max_lag_ns) r.max_lag_ns = t - (t0 + e.t_ns);
    mold::Header h{};
    std::memcpy(h.session, kSession, mold::kSessionLen);
    h.seq = e.seq;
    h.count = e.count;
    std::size_t len = mold::encode_header(pkt, h);
    if (e.count != mold::kHeartbeat && e.count != mold::kEndOfSession) {
      for (uint16_t i = 0; i < e.count; ++i) {
        WireMsg m = plan.msgs[e.seq - 1 + i];
        m.send_ns = t0 + e.intended_ns;
        len += mold::put_block(pkt + len, &m, sizeof m);
      }
    }
    if (::send(fds[e.feed], pkt, len, 0) == static_cast<ssize_t>(len)) ++r.packets_sent;
  }
  ::close(fds[0]);
  ::close(fds[1]);
  return r;
}
