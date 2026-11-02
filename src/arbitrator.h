#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "message.h"
#include "mold.h"

struct ArbStats {
  uint64_t delivered = 0;  // in order + late
  uint64_t late = 0;
  uint64_t lost = 0;  // sequence numbers skipped by declared gaps
  uint64_t gaps = 0;
  uint64_t held = 0;  // messages that waited for an earlier sequence number
  uint64_t dupes = 0;
  uint64_t line_dupes[2] = {0, 0};
  uint64_t session_mismatch = 0;
};

struct Range {
  uint64_t lo, hi;  // [lo, hi)
};

// Merges feeds A and B into one in-order stream and calls sink(const Message&).
// Messages with a sequence number inside a declared gap arrive late and are flagged.
template <class Sink>
class Arbitrator {
 public:
  Arbitrator(Sink& sink, uint64_t gap_timeout_ns, std::string gap_path = "gaps.txt")
      : sink_(sink), timeout_(gap_timeout_ns), gap_path_(std::move(gap_path)) {}

  void on_packet(uint64_t now, int feed, const mold::Header& h, const uint8_t* body, std::size_t len, uint64_t recv_ns) {
    if (!session_set_) { std::memcpy(session_, h.session, mold::kSessionLen); session_set_ = true; }
    if (std::memcmp(session_, h.session, mold::kSessionLen) != 0) { ++st_.session_mismatch; return; }
    if (h.count == mold::kEndOfSession) {
      if (!eos_[feed] && h.seq > next_) declare_gap(h.seq);
      eos_[feed] = true;
      return;
    }
    if (h.count == mold::kHeartbeat) { on_tick(now); return; }
    std::size_t off = 0;
    mold::Block b;
    for (uint16_t i = 0; i < h.count && mold::next_block(body, len, off, b); ++i) {
      if (b.len != sizeof(WireMsg)) continue;
      WireMsg w;
      std::memcpy(&w, b.data, sizeof w);
      Message m{};
      m.seq = h.seq + i;
      m.send_ns = w.send_ns;
      m.recv_ns = recv_ns;
      m.price = w.price;
      m.qty = w.qty;
      std::memcpy(m.sym, w.sym, sizeof m.sym);
      m.feed = static_cast<uint8_t>(feed);
      on_message(now, feed, m.seq, m);
    }
  }

  void on_message(uint64_t now, int feed, uint64_t seq, const Message& m) {
    on_tick(now);
    if (seq == last_seq_[feed]) { ++st_.line_dupes[feed]; return; }
    last_seq_[feed] = seq;
    if (seq < next_) {
      if (in_gap(seq)) { deliver(m, true); ++st_.late; note("late", feed, seq); }
      else ++st_.dupes;
      return;
    }
    if (seq == next_) { deliver(m, false); ++next_; drain(now); return; }
    if (pending_.empty()) pending_since_ = now;
    if (pending_.emplace(seq, m).second) { ++st_.held; note("hold", feed, seq); }
  }

  void on_tick(uint64_t now) {
    if (!pending_.empty() && now - pending_since_ >= timeout_) declare_gap(pending_.begin()->first), drain(now);
  }

  bool done() const { return eos_[0] && eos_[1]; }
  const ArbStats& stats() const { return st_; }
  const std::vector<Range>& gaps() const { return gaps_; }
  const std::vector<std::string>& events() const { return events_; }

 private:
  void deliver(const Message& m, bool late) {
    Message out = m;
    out.late = late;
    sink_(out);
    ++st_.delivered;
  }
  void drain(uint64_t now) {
    while (!pending_.empty() && pending_.begin()->first == next_) {
      deliver(pending_.begin()->second, false);
      pending_.erase(pending_.begin());
      ++next_;
    }
    pending_since_ = now;
  }
  void declare_gap(uint64_t upto) {
    gaps_.push_back({next_, upto});
    st_.lost += upto - next_;
    ++st_.gaps;
    note("gap", -1, next_);
    next_ = upto;
    std::ofstream out(gap_path_, std::ios::trunc);
    for (const Range& r : gaps_) out << r.lo << ' ' << r.hi << '\n';
  }
  bool in_gap(uint64_t seq) const {
    auto it = std::upper_bound(gaps_.begin(), gaps_.end(), seq, [](uint64_t s, const Range& r) { return s < r.hi; });
    return it != gaps_.end() && it->lo <= seq && seq < it->hi;
  }
  void note(const char* kind, int feed, uint64_t seq) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s feed=%c seq=%llu next=%llu", kind, feed < 0 ? '-' : (feed ? 'B' : 'A'),
                  (unsigned long long)seq, (unsigned long long)next_);
    events_.emplace_back(buf);
  }

  Sink& sink_;
  uint64_t timeout_;
  std::string gap_path_;
  uint64_t next_ = 1;
  uint64_t last_seq_[2] = {0, 0};
  std::map<uint64_t, Message> pending_;
  uint64_t pending_since_ = 0;
  std::vector<Range> gaps_;
  std::vector<std::string> events_;
  bool eos_[2] = {false, false};
  bool session_set_ = false;
  char session_[mold::kSessionLen] = {};
  ArbStats st_;
};
