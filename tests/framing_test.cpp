#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mold.h"
#include "sim.h"

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed: %s (line %d)\n", #x, __LINE__); std::exit(1); } } while (0)

int main() {
  uint8_t buf[64];
  mold::Header h{};
  std::memcpy(h.session, "FEEDEX0001", mold::kSessionLen);
  h.seq = 0x0102030405060708ULL;
  h.count = 2;
  std::size_t len = mold::encode_header(buf, h);
  CHECK(len == mold::kHeaderSize);
  CHECK(buf[10] == 0x01 && buf[17] == 0x08 && buf[18] == 0 && buf[19] == 2);
  const char payload[] = "abc";
  len += mold::put_block(buf + len, payload, 3);
  mold::Header d{};
  CHECK(mold::decode_header(buf, len, d));
  CHECK(d.seq == h.seq && d.count == 2 && !std::memcmp(d.session, h.session, mold::kSessionLen));
  std::size_t off = 0;
  mold::Block b;
  CHECK(mold::next_block(buf + mold::kHeaderSize, len - mold::kHeaderSize, off, b));
  CHECK(b.len == 3 && !std::memcmp(b.data, "abc", 3));
  CHECK(!mold::next_block(buf + mold::kHeaderSize, len - mold::kHeaderSize, off, b));

  SimConfig cfg;
  cfg.msgs = 1000;
  Plan p = plan_schedule(cfg);
  CHECK(p.msgs.size() == 1000);
  CHECK(p.reachable <= 1000 && p.reachable > 900);
  for (std::size_t i = 1; i < p.events.size(); ++i) CHECK(p.events[i - 1].t_ns <= p.events[i].t_ns);
  std::puts("framing_test: ok");
  return 0;
}
