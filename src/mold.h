// MoldUDP64 framing (Nasdaq MoldUDP64 Protocol Specification V1.00).
// All numeric header fields are big-endian.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mold {

constexpr std::size_t kHeaderSize = 20;
constexpr std::size_t kSessionLen = 10;
constexpr uint16_t kHeartbeat = 0;
constexpr uint16_t kEndOfSession = 0xFFFF;

struct Header {
  char session[kSessionLen];
  uint64_t seq;    // sequence number of the first message in the packet
  uint16_t count;  // message blocks in the packet (0 = heartbeat, 0xFFFF = end of session)
};

inline void put64(uint8_t* p, uint64_t v) {
  for (int i = 7; i >= 0; --i) { p[i] = static_cast<uint8_t>(v); v >>= 8; }
}
inline void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); }
inline uint64_t get64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
  return v;
}
inline uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

inline std::size_t encode_header(uint8_t* out, const Header& h) {
  std::memcpy(out, h.session, kSessionLen);
  put64(out + 10, h.seq);
  put16(out + 18, h.count);
  return kHeaderSize;
}

inline bool decode_header(const uint8_t* in, std::size_t len, Header& h) {
  if (len < kHeaderSize) return false;
  std::memcpy(h.session, in, kSessionLen);
  h.seq = get64(in + 10);
  h.count = get16(in + 18);
  return true;
}

// Message block: 2-byte length followed by that many data bytes.
struct Block {
  const uint8_t* data;
  uint16_t len;
};

inline bool next_block(const uint8_t* buf, std::size_t len, std::size_t& off, Block& b) {
  if (off + 2 > len) return false;
  b.len = get16(buf + off);
  if (off + 2 + b.len > len) return false;
  b.data = buf + off + 2;
  off += 2 + b.len;
  return true;
}

inline std::size_t put_block(uint8_t* out, const void* data, uint16_t len) {
  put16(out, len);
  std::memcpy(out + 2, data, len);
  return 2 + len;
}

}  // namespace mold
